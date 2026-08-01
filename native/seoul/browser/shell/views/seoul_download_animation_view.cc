// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "seoul/browser/shell/views/seoul_download_animation_view.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "chrome/app/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "seoul/browser/shell/download_animation_geometry.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace seoul {
namespace {

constexpr int kArcPopupSize = 80;
constexpr int kArcTokenSize = 30;
constexpr int kArcInnerRadius = 13;
constexpr int kArcIconSize = 18;
constexpr int kTargetPopupSize = 64;
constexpr int kTargetBoxSize = 40;
constexpr int kTargetIconSize = 20;
constexpr int kTargetCornerRadius = 8;
constexpr base::TimeDelta kTargetAnimationDuration = base::Milliseconds(2200);

double EaseOut(double value) { return 1.0 - std::pow(1.0 - value, 3.0); }

double EaseInOut(double value) {
  return value < 0.5 ? 2.0 * value * value
                     : 1.0 - std::pow(-2.0 * value + 2.0, 2.0) / 2.0;
}

double EaseIn(double value) { return value * value * value; }

float Interpolate(float start, float end, double progress) {
  return static_cast<float>(start + (end - start) * progress);
}

class SeoulDownloadTargetView final : public views::View,
                                      public gfx::LinearAnimation {
public:
  SeoulDownloadTargetView(content::WebContents *web_contents,
                          const SeoulDownloadAnimationParams &params)
      : gfx::LinearAnimation(kTargetAnimationDuration,
                             gfx::LinearAnimation::kDefaultFrameRate,
                             /*delegate=*/nullptr),
        params_(params),
        icon_(gfx::CreateVectorIcon(kSeoulDownloadIcon, kTargetIconSize,
                                    params.accent_color)) {
    popup_ = new views::Widget;
    views::Widget::InitParams widget_params(
        views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_POPUP);
    widget_params.opacity =
        views::Widget::InitParams::WindowOpacity::kTranslucent;
    widget_params.accept_events = false;
    widget_params.parent = web_contents->GetNativeView();
    popup_->Init(std::move(widget_params));
    popup_->SetContentsView(this);
    popup_->SetOpacity(0.0f);
    popup_->Show();
    Start();
  }

  SeoulDownloadTargetView(const SeoulDownloadTargetView &) = delete;
  SeoulDownloadTargetView &operator=(const SeoulDownloadTargetView &) = delete;

  ~SeoulDownloadTargetView() override = default;

private:
  void AnimateToState(double state) override {
    const double elapsed_ms =
        state * kTargetAnimationDuration.InMillisecondsF();
    const float direction = params_.target_on_right ? -1.0f : 1.0f;
    const float offscreen_x =
        params_.target_on_right
            ? static_cast<float>(params_.window_bounds_in_screen.right() + 30)
            : static_cast<float>(params_.window_bounds_in_screen.x() - 30);
    const float target_x = params_.target_in_screen.x();
    const float overshoot_x = target_x + direction * 10.0f;
    float center_x = target_x;

    if (elapsed_ms < 350.0) {
      const double progress = EaseOut(elapsed_ms / 350.0);
      center_x = Interpolate(offscreen_x, overshoot_x, progress);
      scale_ = Interpolate(0.8f, 1.1f, progress);
      opacity_ = Interpolate(0.0f, 1.0f, progress);
    } else if (elapsed_ms < 550.0) {
      const double progress = EaseInOut((elapsed_ms - 350.0) / 200.0);
      center_x = Interpolate(overshoot_x, target_x, progress);
      scale_ = Interpolate(1.1f, 1.0f, progress);
      opacity_ = 1.0f;
    } else if (elapsed_ms < 1750.0) {
      scale_ = 1.0f;
      opacity_ = 1.0f;
    } else if (elapsed_ms < 1900.0) {
      const double progress = EaseIn((elapsed_ms - 1750.0) / 150.0);
      scale_ = Interpolate(1.0f, 0.9f, progress);
      opacity_ = 1.0f;
    } else {
      const double progress =
          EaseIn(std::clamp((elapsed_ms - 1900.0) / 300.0, 0.0, 1.0));
      center_x = Interpolate(target_x, offscreen_x, progress);
      scale_ = Interpolate(0.9f, 0.8f, progress);
      opacity_ = Interpolate(1.0f, 0.0f, progress);
    }

    const gfx::Point center(static_cast<int>(std::round(center_x)),
                            params_.target_in_screen.y());
    popup_->SetBounds(gfx::Rect(center.x() - kTargetPopupSize / 2,
                                center.y() - kTargetPopupSize / 2,
                                kTargetPopupSize, kTargetPopupSize));
    popup_->SetOpacity(std::clamp(opacity_, 0.0f, 1.0f));
    SchedulePaint();

    if (state >= 1.0) {
      popup_->Close();
    }
  }

  void OnPaint(gfx::Canvas *canvas) override {
    views::View::OnPaint(canvas);
    gfx::Transform transform;
    const gfx::PointF center = gfx::RectF(GetLocalBounds()).CenterPoint();
    transform.Translate(center.x(), center.y());
    transform.Scale(scale_, scale_);
    transform.Translate(-center.x(), -center.y());
    canvas->Save();
    canvas->Transform(transform);

    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(params_.toolbar_color);
    const gfx::RectF box((kTargetPopupSize - kTargetBoxSize) / 2.0f,
                         (kTargetPopupSize - kTargetBoxSize) / 2.0f,
                         kTargetBoxSize, kTargetBoxSize);
    canvas->DrawRoundRect(box, kTargetCornerRadius, flags);
    canvas->DrawImageInt(icon_, (kTargetPopupSize - icon_.width()) / 2,
                         (kTargetPopupSize - icon_.height()) / 2);
    canvas->Restore();
  }

  const SeoulDownloadAnimationParams params_;
  const gfx::ImageSkia icon_;
  raw_ptr<views::Widget> popup_ = nullptr;
  float scale_ = 0.8f;
  float opacity_ = 0.0f;
};

class SeoulDownloadArcView final : public views::View,
                                   public gfx::LinearAnimation {
public:
  SeoulDownloadArcView(content::WebContents *web_contents,
                       const SeoulDownloadAnimationParams &params)
      : gfx::LinearAnimation(params.duration,
                             gfx::LinearAnimation::kDefaultFrameRate,
                             /*delegate=*/nullptr),
        params_(params), token_(CreateToken(params)) {
    popup_ = new views::Widget;
    views::Widget::InitParams widget_params(
        views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_POPUP);
    widget_params.opacity =
        views::Widget::InitParams::WindowOpacity::kTranslucent;
    widget_params.accept_events = false;
    widget_params.parent = web_contents->GetNativeView();
    popup_->Init(std::move(widget_params));
    popup_->SetContentsView(this);
    popup_->SetOpacity(0.0f);
    popup_->Show();

    if (!params.target_button_visible) {
      new SeoulDownloadTargetView(web_contents, params);
    }
    Start();
  }

  SeoulDownloadArcView(const SeoulDownloadArcView &) = delete;
  SeoulDownloadArcView &operator=(const SeoulDownloadArcView &) = delete;

  ~SeoulDownloadArcView() override = default;

private:
  static gfx::ImageSkia
  CreateToken(const SeoulDownloadAnimationParams &params) {
    gfx::ImageSkia icon = gfx::CreateVectorIcon(
        kSeoulDownloadIcon, kArcIconSize, params.accent_color);
    gfx::ImageSkia inner =
        gfx::ImageSkiaOperations::CreateImageWithCircleBackground(
            kArcInnerRadius, params.toolbar_color, icon);
    return gfx::ImageSkiaOperations::CreateImageWithCircleBackground(
        kArcTokenSize / 2, params.hover_color, inner);
  }

  void AnimateToState(double state) override {
    const DownloadArcFrame frame = CalculateDownloadArcFrame(
        gfx::PointF(params_.start_in_screen),
        gfx::PointF(params_.target_in_screen),
        gfx::RectF(params_.window_bounds_in_screen), state);
    scale_ = frame.scale;
    smoothed_rotation_degrees_ +=
        (frame.rotation_degrees - smoothed_rotation_degrees_) * 0.01f;
    const gfx::Point center = gfx::ToRoundedPoint(frame.center);
    popup_->SetBounds(gfx::Rect(center.x() - kArcPopupSize / 2,
                                center.y() - kArcPopupSize / 2, kArcPopupSize,
                                kArcPopupSize));
    popup_->SetOpacity(frame.opacity);
    SchedulePaint();

    if (state >= 1.0) {
      popup_->Close();
    }
  }

  void OnPaint(gfx::Canvas *canvas) override {
    views::View::OnPaint(canvas);
    gfx::Transform transform;
    const gfx::PointF center = gfx::RectF(GetLocalBounds()).CenterPoint();
    transform.Translate(center.x(), center.y());
    transform.Rotate(smoothed_rotation_degrees_);
    transform.Scale(scale_, scale_);
    transform.Translate(-center.x(), -center.y());
    canvas->Save();
    canvas->Transform(transform);
    canvas->DrawImageInt(token_, (kArcPopupSize - token_.width()) / 2,
                         (kArcPopupSize - token_.height()) / 2);
    canvas->Restore();
  }

  const SeoulDownloadAnimationParams params_;
  const gfx::ImageSkia token_;
  raw_ptr<views::Widget> popup_ = nullptr;
  float scale_ = 0.5f;
  float smoothed_rotation_degrees_ = 0.0f;
};

} // namespace

void ShowSeoulDownloadStartedAnimation(
    content::WebContents *web_contents,
    const SeoulDownloadAnimationParams &params) {
  if (!web_contents || params.duration <= base::TimeDelta() ||
      params.window_bounds_in_screen.IsEmpty()) {
    return;
  }
  new SeoulDownloadArcView(web_contents, params);
}

} // namespace seoul
