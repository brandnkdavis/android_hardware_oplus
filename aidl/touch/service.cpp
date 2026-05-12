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
#include <climits>
#include <cstdlib>
#include <memory>

using aidl::vendor::lineage::touch::GestureInjector;
using aidl::vendor::lineage::touch::GloveMode;
using aidl::vendor::lineage::touch::HighTouchPollingRate;
using aidl::vendor::lineage::touch::TouchscreenGesture;
using aidl::vendor::oplus::hardware::touch::IOplusTouch;

namespace {

void seedDoubleTapWake(const std::shared_ptr<IOplusTouch>& oplusTouch) {
    if (!oplusTouch) {
        return;
    }

    std::string tmp;
    if (!oplusTouch->touchReadNodeFile(OplusTouchConstants::DEFAULT_TP_IC_ID,
                                       OplusTouchConstants::DOUBLE_TAP_INDEP_NODE, &tmp)
                 .isOk()) {
        return;
    }

    errno = 0;
    char* end = nullptr;
    const long parsed = strtol(tmp.c_str(), &end, 16);
    if (errno != 0 || end == tmp.c_str() || parsed < 0 || parsed > INT_MAX) {
        LOG(WARNING) << "Failed to parse double-tap bitmask from touch daemon";
        return;
    }

    const int contents = static_cast<int>(parsed) | OplusTouchConstants::DOUBLE_TAP_GESTURE;
    oplusTouch->touchWriteNodeFileOneWay(OplusTouchConstants::DEFAULT_TP_IC_ID,
                                         OplusTouchConstants::DOUBLE_TAP_ENABLE_NODE, "1");
    oplusTouch->touchWriteNodeFileOneWay(OplusTouchConstants::DEFAULT_TP_IC_ID,
                                         OplusTouchConstants::DOUBLE_TAP_INDEP_NODE,
                                         std::to_string(contents));
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
