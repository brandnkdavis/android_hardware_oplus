/*
 * SPDX-FileCopyrightText: 2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vendor.lineage.touch-service.oplus.GestureInjector"

#include "GestureInjector.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include <log/log.h>

namespace {

constexpr const char* kHbpCorePath = "/dev/hbp_core";
constexpr const char* kInputDir = "/dev/input";
constexpr const char* kFallbackEventPath = "/dev/input/event7";
constexpr int kKeyF4 = 62;
constexpr int kGestureKeycodeBase = 246;
constexpr int kInjectKeycodeMin = 247;
constexpr int kInjectKeycodeMax = 264;
constexpr uint32_t kHbpGestureIoctl = 0xD505;

}  // anonymous namespace

namespace aidl {
namespace vendor {
namespace lineage {
namespace touch {

GestureInjector::GestureInjector() {
    mHbpCoreFd = open(kHbpCorePath, O_RDONLY);
    if (mHbpCoreFd < 0) {
        ALOGE("Failed to open /dev/hbp_core: %s", strerror(errno));
        return;
    }

    const std::string eventPath = findTouchpanelDevice();
    mEventFd = open(eventPath.c_str(), O_RDONLY | O_NONBLOCK);
    if (mEventFd < 0) {
        ALOGE("Failed to find touchpanel event device");
        return;
    }

    if (!createUinputDevice()) {
        ALOGE("Failed to create uinput gesture device");
        return;
    }

    mRunning = true;
    ALOGI("GestureInjector started");
    mThread = std::thread(&GestureInjector::run, this);
}

GestureInjector::~GestureInjector() {
    mRunning = false;
    if (mThread.joinable()) mThread.join();
    if (mUinputFd >= 0) {
        ioctl(mUinputFd, UI_DEV_DESTROY);
        close(mUinputFd);
    }
    if (mEventFd >= 0) close(mEventFd);
    if (mHbpCoreFd >= 0) close(mHbpCoreFd);
}

std::string GestureInjector::findTouchpanelDevice() {
    DIR* dir = opendir(kInputDir);
    if (!dir) {
        ALOGE("opendir(%s) failed: %s", kInputDir, strerror(errno));
        return kFallbackEventPath;
    }

    std::string result;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        std::string devPath = std::string(kInputDir) + "/" + entry->d_name;
        int fd = open(devPath.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        char name[256] = {};
        int ret = ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        close(fd);
        if (ret < 0) continue;

        if (strstr(name, "hbp") || strstr(name, "touchpanel") || strstr(name, "Synaptics") ||
            strstr(name, "oplus")) {
            ALOGI("Using touchpanel event device: %s (%s)", devPath.c_str(), name);
            result = devPath;
            break;
        }
    }
    closedir(dir);

    if (result.empty()) {
        ALOGW("No touchpanel event device found by EVIOCGNAME scan, falling back to /dev/input/event7");
        return kFallbackEventPath;
    }
    return result;
}

bool GestureInjector::createUinputDevice() {
    mUinputFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (mUinputFd < 0) {
        ALOGE("Failed to open /dev/uinput: %s", strerror(errno));
        return false;
    }

    if (ioctl(mUinputFd, UI_SET_EVBIT, EV_KEY) < 0) {
        ALOGE("UI_SET_EVBIT failed: %s", strerror(errno));
        return false;
    }

    for (int kc = kInjectKeycodeMin; kc <= kInjectKeycodeMax; kc++) {
        if (ioctl(mUinputFd, UI_SET_KEYBIT, kc) < 0) {
            ALOGE("UI_SET_KEYBIT(%d) failed: %s", kc, strerror(errno));
            return false;
        }
    }

    struct uinput_setup usetup = {};
    usetup.id.bustype = BUS_VIRTUAL;
    usetup.id.vendor = 0x1234;
    usetup.id.product = 0x5678;
    strncpy(usetup.name, "lineage_touch_gestures", sizeof(usetup.name) - 1);

    if (ioctl(mUinputFd, UI_DEV_SETUP, &usetup) < 0) {
        ALOGE("UI_DEV_SETUP failed: %s", strerror(errno));
        return false;
    }

    if (ioctl(mUinputFd, UI_DEV_CREATE) < 0) {
        ALOGE("UI_DEV_CREATE failed: %s", strerror(errno));
        return false;
    }

    ALOGI("uinput gesture device created (keycodes %d-%d)", kInjectKeycodeMin, kInjectKeycodeMax);
    return true;
}

void GestureInjector::injectKeycode(int keycode) {
    struct input_event ev[3];
    memset(ev, 0, sizeof(ev));
    ev[0].type = EV_KEY; ev[0].code = static_cast<__u16>(keycode); ev[0].value = 1;
    ev[1].type = EV_KEY; ev[1].code = static_cast<__u16>(keycode); ev[1].value = 0;
    ev[2].type = EV_SYN; ev[2].code = SYN_REPORT; ev[2].value = 0;

    if (write(mUinputFd, ev, sizeof(ev)) != static_cast<ssize_t>(sizeof(ev))) {
        ALOGE("Failed to inject keycode %d: %s", keycode, strerror(errno));
        return;
    }
    ALOGI("Injected gesture keycode %d", keycode);
}

void GestureInjector::run() {
    struct pollfd pfd = {.fd = mEventFd, .events = POLLIN};

    while (mRunning) {
        if (poll(&pfd, 1, -1) < 0) {
            ALOGE("poll error: %s", strerror(errno));
            break;
        }

        struct input_event ev;
        while (read(mEventFd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
            if (ev.type != EV_KEY || ev.code != kKeyF4 || ev.value != 1) continue;

            uint32_t gestureType = 0;
            if (ioctl(mHbpCoreFd, kHbpGestureIoctl, &gestureType) < 0) {
                ALOGE("ioctl 0xD505 on hbp_core failed: %s", strerror(errno));
                continue;
            }

            ALOGI("KEY_F4 received, gesture_type=%u", gestureType);
            if (gestureType > 0) {
                injectKeycode(kGestureKeycodeBase + gestureType);
            }
        }
    }
}

}  // namespace touch
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
