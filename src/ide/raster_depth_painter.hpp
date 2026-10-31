#pragma once
#include <QColor>
#include <QImage>
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace penta {
// Physical, top-down screen coordinates. Depth is normalized camera depth;
// smaller values are nearer. Empty vertexDepths uses the constant depth field.
struct RasterPrimitive {
    QPolygonF points;
    QColor color;
    double width = 1, depth = 0;
    int kind = 0; // 0: round-capped polyline; 1: filled polygon; 2: point disks.
    std::vector<double> vertexDepths;
};

// A host may tighten these limits; requests above the production maxima are
// clamped. Also permits small, deterministic quota tests without heavy renders.
struct RasterDepthBudget {
    std::size_t maximumPixels = 16000000;
    std::uint64_t maximumOperations = 250000000;
    std::size_t maximumBytes = 256U * 1024U * 1024U;
};

// Pure CPU rendering, including four subpixel coverage/depth samples. Pixels
// outside geometry are transparent. The caller may composite a background and
// annotations afterwards. Cancellation or an explicit work/storage quota returns
// a null image and a diagnostic. No QWidget, VTK, or graphics context is used.
QImage rasterizePlotGeometry(QSize physical,
    const std::vector<RasterPrimitive>& primitives, QRectF clip,
    std::shared_ptr<std::atomic_bool> cancelled, QString* error = nullptr);
QImage rasterizePlotGeometry(QSize physical,
    const std::vector<RasterPrimitive>& primitives, QRectF clip,
    std::shared_ptr<std::atomic_bool> cancelled, RasterDepthBudget budget,
    QString* error = nullptr);
}
