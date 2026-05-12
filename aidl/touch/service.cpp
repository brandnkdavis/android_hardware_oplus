/*
 * SPDX-FileCopyrightText: 2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vendor.lineage.touch-service.oplus"

#include "GestureInjector.h"
#include "GloveMode.h"
#include "HighTouchPollingRate.h"
#include "TouchscreenGesture.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <OplusTouchConstants.h>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <memory>
#include <thread>

using aidl::vendor::lineage::touch::GestureInjector;
using aidl::vendor::lineage::touch::GloveMode;
using aidl::vendor::lineage::touch::HighTouchPollingRate;
using aidl::vendor::lineage::touch::TouchscreenGesture;
using aidl::vendor::oplus::hardware::touch::IOplusTouch;

namespace {

bool applyDoubleTapWake(const std::shared_ptr<IOplusTouch>& oplusTouch) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        std::string tmp;
        if (oplusTouch->touchReadNodeFile(OplusTouchConstants::DEFAULT_TP_IC_ID,
                                          OplusTouchConstants::DOUBLE_TAP_INDEP_NODE, &tmp)
                    .isOk()) {
            errno = 0;
            char* end = nullptr;
            const long parsed = strtol(tmp.c_str(), &end, 16);
            if (errno == 0 && end != tmp.c_str() && parsed >= 0 && parsed <= INT_MAX) {
                const int contents =
                        static_cast<int>(parsed) | OplusTouchConstants::DOUBLE_TAP_GESTURE;
                if (oplusTouch->touchWriteNodeFileOneWay(
                            OplusTouchConstants::DEFAULT_TP_IC_ID,
                            OplusTouchConstants::DOUBLE_TAP_ENABLE_NODE, "1")
                            .isOk() &&
                    oplusTouch->touchWriteNodeFileOneWay(
                            OplusTouchConstants::DEFAULT_TP_IC_ID,
                            OplusTouchConstants::DOUBLE_TAP_INDEP_NODE,
                            std::to_string(contents))
                            .isOk()) {
                    LOG(INFO) << "Seeded DT2W bitmask at startup: " << contents;
                    return true;
                }
            } else {
                LOG(WARNING) << "Failed to parse double-tap bitmask from touch daemon";
                return false;
            }
        }

        LOG(WARNING) << "DT2W seed attempt " << (attempt + 1)
                     << " failed; retrying after touch service settles";
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    LOG(WARNING) << "Giving up on DT2W startup seed after retries";
    return false;
}

void seedDoubleTapWake(const std::shared_ptr<IOplusTouch>& oplusTouch) {
    if (!oplusTouch) {
        return;
    }

    if (applyDoubleTapWake(oplusTouch)) {
        std::thread([oplusTouch]() {
            for (int attempt = 0; attempt < 6; ++attempt) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                if (applyDoubleTapWake(oplusTouch)) {
                    LOG(INFO) << "Refreshed DT2W bitmask after boot";
                    return;
                }
            }
            LOG(WARNING) << "DT2W post-boot refresh worker exhausted retries";
        }).detach();
    }
}

}  // namespace

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    const std::string instance = std::string() + IOplusTouch::descriptor + "/default";
    std::shared_ptr<IOplusTouch> oplusTouch =
            USE_OPLUSTOUCH ? IOplusTouch::fromBinder(ndk::SpAIBinder(
                                     AServiceManager_waitForService(instance.c_str())))
                           : nullptr;

    std::shared_ptr<GloveMode> gm =
            ENABLE_GM ? ndk::SharedRefBase::make<GloveMode>(oplusTouch) : nullptr;
    std::shared_ptr<HighTouchPollingRate> htpr =
            ENABLE_HTPR ? ndk::SharedRefBase::make<HighTouchPollingRate>(oplusTouch) : nullptr;
    std::shared_ptr<TouchscreenGesture> tg =
            ENABLE_TG ? ndk::SharedRefBase::make<TouchscreenGesture>(oplusTouch) : nullptr;

    seedDoubleTapWake(oplusTouch);

    std::unique_ptr<GestureInjector> gestureInjector;
    if (ENABLE_TG) {
        gestureInjector = std::make_unique<GestureInjector>();
    }

    if (gm) {
        const std::string instance = std::string(GloveMode::descriptor) + "/default";
        const binder_status_t status =
                AServiceManager_addService(gm->asBinder().get(), instance.c_str());
        CHECK_EQ(status, STATUS_OK) << "Failed to add service " << instance << " " << status;
    }

    if (htpr) {
        const std::string instance = std::string(HighTouchPollingRate::descriptor) + "/default";
        const binder_status_t status =
                AServiceManager_addService(htpr->asBinder().get(), instance.c_str());
        CHECK_EQ(status, STATUS_OK) << "Failed to add service " << instance << " " << status;
    }

    if (tg) {
        const std::string instance = std::string(TouchscreenGesture::descriptor) + "/default";
        const binder_status_t status =
                AServiceManager_addService(tg->asBinder().get(), instance.c_str());
        CHECK_EQ(status, STATUS_OK) << "Failed to add service " << instance << " " << status;
    }

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
