/*
 * SPDX-FileCopyrightText: 2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vendor.lineage.touch-service.oplus"

#include <android-base/file.h>
#include <android-base/strings.h>

#include <cerrno>
#include <climits>
#include <cstdlib>

#include <OplusTouchConstants.h>
#include <TouchscreenGestureConfig.h>

using ::android::base::ReadFileToString;
using ::android::base::Trim;
using ::android::base::WriteStringToFile;

namespace {

constexpr const char* kGestureEnableIndepPath = "/proc/touchpanel/double_tap_enable_indep";
constexpr int kDoubleTapGestureBit = 1;
constexpr int kDoubleTapGestureMask = 1 << kDoubleTapGestureBit;

bool parseGestureBitmask(const std::string& value, int* bitmask) {
    const std::string trimmedValue = Trim(value);
    if (trimmedValue.empty()) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const long parsedValue = strtol(trimmedValue.c_str(), &end, 16);
    if (errno != 0 || end == trimmedValue.c_str() || *end != '\0' || parsedValue < 0 ||
        parsedValue > INT_MAX) {
        return false;
    }

    *bitmask = static_cast<int>(parsedValue);
    return true;
}

}  // anonymous namespace

namespace aidl {
namespace vendor {
namespace lineage {
namespace touch {

TouchscreenGesture::TouchscreenGesture(std::shared_ptr<IOplusTouch> oplusTouch)
    : mOplusTouch(std::move(oplusTouch)) {}

ndk::ScopedAStatus TouchscreenGesture::getSupportedGestures(std::vector<Gesture>* _aidl_return) {
    std::vector<Gesture> gestures;

    for (const auto& [id, name] : kGestureNames) {
        if (kSupportedGestures & (1 << id)) {
            gestures.push_back({static_cast<int>(gestures.size()), name, kGestureStartKey + id});
        }
    }

    *_aidl_return = gestures;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TouchscreenGesture::setGestureEnabled(const Gesture& gesture, bool enabled) {
    int contents = 0;
    const int gestureBit = gesture.keycode - kGestureStartKey;

    if (gestureBit <= kDoubleTapGestureBit || gestureBit >= 31) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    if (std::string tmp; mOplusTouch) {
        if (mGestureBitmask < 0) {
            mOplusTouch->touchReadNodeFile(OplusTouchConstants::DEFAULT_TP_IC_ID,
                                           OplusTouchConstants::DOUBLE_TAP_INDEP_NODE, &tmp);
            if (!parseGestureBitmask(tmp, &mGestureBitmask)) {
                return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
            }
        }
        contents = mGestureBitmask;
        if (mOplusTouch->touchReadNodeFile(OplusTouchConstants::DEFAULT_TP_IC_ID,
                                           OplusTouchConstants::DOUBLE_TAP_INDEP_NODE, &tmp)
                    .isOk()) {
            int currentBitmask = 0;
            if (parseGestureBitmask(tmp, &currentBitmask)) {
                contents = (contents & ~kDoubleTapGestureMask) |
                           (currentBitmask & kDoubleTapGestureMask);
            }
        }
    } else if (ReadFileToString(kGestureEnableIndepPath, &tmp)) {
        if (!parseGestureBitmask(tmp, &contents)) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        }
    } else {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    if (enabled) {
        contents |= (1 << gestureBit);
    } else {
        contents &= ~(1 << gestureBit);
    }

    if (mOplusTouch) {
        mGestureBitmask = contents;
        mOplusTouch->touchWriteNodeFileOneWay(OplusTouchConstants::DEFAULT_TP_IC_ID,
                                              OplusTouchConstants::DOUBLE_TAP_ENABLE_NODE, "1");
        mOplusTouch->touchWriteNodeFileOneWay(OplusTouchConstants::DEFAULT_TP_IC_ID,
                                              OplusTouchConstants::DOUBLE_TAP_INDEP_NODE,
                                              std::to_string(contents));
    } else if (!WriteStringToFile(std::to_string(contents), kGestureEnableIndepPath, true)) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    return ndk::ScopedAStatus::ok();
}

}  // namespace touch
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
