/*
 * SPDX-FileCopyrightText: 2024-2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <aidl/android/hardware/power/BnPower.h>
#include <aidl/vendor/oplus/hardware/touch/IOplusTouch.h>

#include <android-base/logging.h>
#include <android/binder_manager.h>

#include <OplusTouchConstants.h>
#include <cerrno>
#include <climits>
#include <cstdlib>

using aidl::android::hardware::power::Mode;
using aidl::vendor::oplus::hardware::touch::IOplusTouch;

#ifdef LIBPERFMGR_EXT
namespace aidl::google::hardware::power::impl::pixel {
#else
namespace aidl::android::hardware::power::impl {
#endif

bool isDeviceSpecificModeSupported(Mode type, bool* _aidl_return) {
    switch (type) {
        case Mode::DOUBLE_TAP_TO_WAKE:
            *_aidl_return = true;
            return true;
        default:
            return false;
    }
}

bool setDeviceSpecificMode(Mode type, bool enabled) {
    switch (type) {
        case Mode::DOUBLE_TAP_TO_WAKE: {
            std::string tmp;
            int contents = 0;

            const std::string instance = std::string() + IOplusTouch::descriptor + "/default";
            std::shared_ptr<IOplusTouch> oplusTouch = IOplusTouch::fromBinder(
                    ndk::SpAIBinder(AServiceManager_waitForService(instance.c_str())));
            LOG(INFO) << "Power mode: " << toString(type) << " isDoubleTapEnabled: " << enabled;

            if (oplusTouch->touchReadNodeFile(OplusTouchConstants::DEFAULT_TP_IC_ID,
                                              OplusTouchConstants::DOUBLE_TAP_INDEP_NODE, &tmp)
                        .isOk()) {
                errno = 0;
                char* end = nullptr;
                const long parsed = strtol(tmp.c_str(), &end, 16);
                if (errno == 0 && end != tmp.c_str() && parsed >= 0 && parsed <= INT_MAX) {
                    contents = static_cast<int>(parsed);
                } else {
                    LOG(WARNING) << "Failed to parse current DT2W bitmask, defaulting to 0";
                }
            } else {
                LOG(WARNING) << "Failed to read current DT2W bitmask, defaulting to 0";
            }

            if (enabled) {
                contents |= OplusTouchConstants::DOUBLE_TAP_GESTURE;
            } else {
                contents &= ~OplusTouchConstants::DOUBLE_TAP_GESTURE;
            }

            oplusTouch->touchWriteNodeFileOneWay(OplusTouchConstants::DEFAULT_TP_IC_ID,
                                                 OplusTouchConstants::DOUBLE_TAP_ENABLE_NODE, "1");
            oplusTouch->touchWriteNodeFileOneWay(OplusTouchConstants::DEFAULT_TP_IC_ID,
                                                 OplusTouchConstants::DOUBLE_TAP_INDEP_NODE,
                                                 std::to_string(contents));
            LOG(INFO) << "Power mode: wrote DT2W bitmask " << contents;
            return true;
        }
        default:
            return false;
    }
}

}  // namespace
