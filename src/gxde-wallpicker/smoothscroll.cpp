#include "smoothscroll.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QInputDevice>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <cmath>
#include <functional>

namespace GxdeWallpaper {

namespace {

// 跟随滚轮的时间常数：每帧按指数逼近目标，越小越跟手、越大越"滑"
constexpr qreal kFollowTauMs = 70;
constexpr qreal kMaxOvershoot = 60;   // 回弹最多把内容拉出边界的距离
constexpr qreal kKickRatio = 0.35;    // 滚轮超出量换算成回弹幅度的比例
constexpr int kKickMs = 450;          // 滚轮触边：冲出 + 弹回
constexpr qreal kKickPeakAt = 0.3;    // 冲出到顶点占整段时长的比例
constexpr int kSpringMs = 300;        // 触控板松手后弹回
constexpr int kReleaseTimeoutMs = 150; // 没收到 ScrollEnd 时的兜底

// 每帧回调一次的无限时长动画（走 Qt 的动画定时器，与其他动画同步）
class FrameTicker : public QAbstractAnimation
{
public:
    explicit FrameTicker(std::function<void(int)> onFrame, QObject *parent)
        : QAbstractAnimation(parent), m_onFrame(std::move(onFrame)) {}

    int duration() const override { return -1; }

protected:
    void updateCurrentTime(int msecs) override { m_onFrame(msecs); }

private:
    std::function<void(int)> m_onFrame;
};

class SmoothScroller : public QObject
{
public:
    explicit SmoothScroller(QAbstractScrollArea *area)
        : QObject(area)
        , m_area(qobject_cast<QScrollArea *>(area))
        , m_bar(area->verticalScrollBar())
    {
        // 滚轮只改目标，每帧让当前位置向目标指数逼近：连续滚动时始终跟手，
        // 不会因为每格重启一段缓动而一顿一顿
        m_follow = new FrameTicker([this](int msecs) { stepFollow(msecs); }, this);

        // 回弹：0..1 线性进度，形状在 valueChanged 里按模式计算
        m_bounce = new QVariantAnimation(this);
        m_bounce->setStartValue(0.0);
        m_bounce->setEndValue(1.0);
        connect(m_bounce, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) { stepBounce(value.toReal()); });
        connect(m_bounce, &QVariantAnimation::finished, this,
                [this] { setOvershoot(0); });

        m_release = new QTimer(this);
        m_release->setSingleShot(true);
        m_release->setInterval(kReleaseTimeoutMs);
        connect(m_release, &QTimer::timeout, this, [this] { springBack(); });

        // 拖动滚动条、键盘翻页、内容重建时让位，不和用户抢位置
        auto yield = [this] {
            m_follow->stop();
            m_pendingKick = 0;
        };
        connect(m_bar, &QScrollBar::sliderPressed, this, yield);
        connect(m_bar, &QScrollBar::actionTriggered, this, yield);
        connect(m_bar, &QScrollBar::rangeChanged, this, [this, yield] {
            yield();
            m_bounce->stop();
            setOvershoot(0);
        });

        area->viewport()->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Wheel)
            return QObject::eventFilter(watched, event);

        auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers() != Qt::NoModifier)
            return QObject::eventFilter(watched, event);

        const bool touchpad = wheel->phase() != Qt::NoScrollPhase
            || (wheel->device()
                && wheel->device()->type() == QInputDevice::DeviceType::TouchPad);
        if (touchpad)
            return handleTouchpad(wheel) || QObject::eventFilter(watched, event);
        if (wheel->angleDelta().y() == 0)
            return QObject::eventFilter(watched, event);

        // 鼠标滚轮：每格距离与 Qt 默认一致（滚动行数 × 单步，最多一页），只是改为滑动过去
        m_bounce->stop();
        setOvershoot(0);
        const int notch = qMin(QApplication::wheelScrollLines() * m_bar->singleStep(),
                               m_bar->pageStep());
        const bool following = m_follow->state() == QAbstractAnimation::Running;
        if (!following)
            m_pos = m_target = m_bar->value();
        const qreal wanted = m_target - wheel->angleDelta().y() / 120.0 * notch;
        m_target = qBound<qreal>(m_bar->minimum(), wanted, m_bar->maximum());

        // 超出边界的部分转成回弹（顶部为正：内容被往下拉）；还没到边就等追上再弹
        const qreal excess = m_target - wanted;
        if (std::abs(m_target - m_pos) < 0.5) {
            m_follow->stop();
            m_pendingKick = 0;
            if (excess != 0)
                kick(excess);
            return true;
        }
        m_pendingKick = excess != 0 ? m_pendingKick + excess : 0; // 往回滚就取消待弹
        if (!following) {
            m_lastFrame = 0;
            m_follow->start();
        }
        return true;
    }

private:
    enum class BounceMode { Kick, Spring };

    // 触控板：贴边继续推时内容跟手拉出（越拉越重），松手弹回；其余交给默认滚动
    bool handleTouchpad(QWheelEvent *wheel)
    {
        if (!m_area)
            return false;
        if (wheel->phase() == Qt::ScrollEnd) {
            springBack();
            return false;
        }

        const qreal dy = wheel->pixelDelta().isNull() ? wheel->angleDelta().y() / 8.0
                                                      : wheel->pixelDelta().y();
        const bool atTop = m_bar->value() <= m_bar->minimum();
        const bool atBottom = m_bar->value() >= m_bar->maximum();
        if (m_overshoot == 0 && !(atTop && dy > 0) && !(atBottom && dy < 0))
            return false;

        m_bounce->stop();
        qreal next;
        if ((m_overshoot >= 0 && dy > 0) || (m_overshoot <= 0 && dy < 0)) {
            const qreal resistance = 1 - qMin(1.0, qAbs(m_overshoot) / kMaxOvershoot);
            next = m_overshoot + dy * 0.5 * resistance;
        } else {
            next = m_overshoot + dy; // 往回推不加阻力，越过 0 即回到正常滚动
            if ((m_overshoot > 0 && next < 0) || (m_overshoot < 0 && next > 0))
                next = 0;
        }
        setOvershoot(next);
        m_release->start();
        return true;
    }

    void stepFollow(int msecs)
    {
        const int dt = qMax(1, msecs - m_lastFrame);
        m_lastFrame = msecs;
        m_pos += (m_target - m_pos) * (1 - std::exp(-dt / kFollowTauMs));
        if (std::abs(m_target - m_pos) < 0.5) {
            m_pos = m_target;
            m_bar->setValue(qRound(m_pos));
            m_follow->stop();
            const bool atEdge = m_pos <= m_bar->minimum() || m_pos >= m_bar->maximum();
            if (m_pendingKick != 0 && atEdge)
                kick(m_pendingKick);
            m_pendingKick = 0;
            return;
        }
        m_bar->setValue(qRound(m_pos));
    }

    void kick(qreal excess)
    {
        if (!m_area)
            return;
        m_bounceMode = BounceMode::Kick;
        m_bounceFrom = m_overshoot;
        m_bouncePeak = qBound(-kMaxOvershoot, excess * kKickRatio, kMaxOvershoot);
        m_bounce->stop();
        m_bounce->setDuration(kKickMs);
        m_bounce->start();
    }

    void springBack()
    {
        m_release->stop();
        if (m_overshoot == 0 || m_bounce->state() == QAbstractAnimation::Running)
            return;
        m_bounceMode = BounceMode::Spring;
        m_bounceFrom = m_overshoot;
        m_bounce->stop();
        m_bounce->setDuration(kSpringMs);
        m_bounce->start();
    }

    void stepBounce(qreal t)
    {
        static const QEasingCurve out(QEasingCurve::OutCubic);
        static const QEasingCurve outQuad(QEasingCurve::OutQuad);
        if (m_bounceMode == BounceMode::Spring) {
            setOvershoot(m_bounceFrom * (1 - out.valueForProgress(t)));
        } else if (t < kKickPeakAt) {
            const qreal k = outQuad.valueForProgress(t / kKickPeakAt);
            setOvershoot(m_bounceFrom + (m_bouncePeak - m_bounceFrom) * k);
        } else {
            const qreal k = out.valueForProgress((t - kKickPeakAt) / (1 - kKickPeakAt));
            setOvershoot(m_bouncePeak * (1 - k));
        }
    }

    // 只挪动内容控件，不动滚动条的值；归零时精确复位
    void setOvershoot(qreal overshoot)
    {
        m_overshoot = overshoot;
        if (!m_area || !m_area->widget())
            return;
        QWidget *content = m_area->widget();
        content->move(content->x(), -m_bar->value() + qRound(overshoot));
    }

    QScrollArea *m_area; // 非 QScrollArea 时只做平滑滚动，不回弹
    QScrollBar *m_bar;
    FrameTicker *m_follow;
    QVariantAnimation *m_bounce;
    QTimer *m_release;
    qreal m_pos = 0;    // 带小数的当前位置（滚动条只接受整数）
    qreal m_target = 0;
    int m_lastFrame = 0;
    qreal m_pendingKick = 0;
    qreal m_overshoot = 0;
    qreal m_bounceFrom = 0;
    qreal m_bouncePeak = 0;
    BounceMode m_bounceMode = BounceMode::Kick;
};

} // namespace

void enableSmoothScroll(QAbstractScrollArea *area)
{
    if (area)
        new SmoothScroller(area);
}

} // namespace GxdeWallpaper
