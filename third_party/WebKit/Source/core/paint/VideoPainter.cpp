// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/paint/VideoPainter.h"

#include "core/dom/Document.h"
#include "core/frame/FrameView.h"
#include "core/html/HTMLVideoElement.h"
#include "core/layout/LayoutVideo.h"
#include "core/paint/ImagePainter.h"
#include "core/paint/LayoutObjectDrawingRecorder.h"
#include "core/paint/PaintInfo.h"
#include "platform/geometry/LayoutPoint.h"
#include "platform/graphics/paint/ForeignLayerDisplayItem.h"

namespace blink {

void VideoPainter::paintReplaced(const PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    WebMediaPlayer* mediaPlayer = m_layoutVideo.mediaElement()->webMediaPlayer();
    bool displayingPoster = m_layoutVideo.videoElement()->shouldDisplayPosterImage();
    if (!displayingPoster && !mediaPlayer)
        return;

    LayoutRect rect(m_layoutVideo.videoBox());
    if (rect.isEmpty())
        return;
    rect.moveBy(paintOffset);

    if (LayoutObjectDrawingRecorder::useCachedDrawingIfPossible(paintInfo.context, m_layoutVideo, paintInfo.phase))
        return;

    GraphicsContext& context = paintInfo.context;
    LayoutRect contentRect = m_layoutVideo.contentBoxRect();
    contentRect.moveBy(paintOffset);

    // Video frames are only painted in software for printing or capturing node images via web APIs.
    bool forceSoftwareVideoPaint = paintInfo.getGlobalPaintFlags() & GlobalPaintFlattenCompositingLayers;

    bool paintWithForeignLayer =
        !displayingPoster && !forceSoftwareVideoPaint
        && RuntimeEnabledFeatures::slimmingPaintV2Enabled();
    if (paintWithForeignLayer) {
        if (WebLayer* layer = m_layoutVideo.mediaElement()->platformLayer()) {
            IntRect pixelSnappedRect = pixelSnappedIntRect(contentRect);
            recordForeignLayer(
                context, m_layoutVideo, DisplayItem::ForeignLayerVideo, layer,
                pixelSnappedRect.location(), pixelSnappedRect.size());
            return;
        }
    }

    LayoutObjectDrawingRecorder drawingRecorder(context, m_layoutVideo, paintInfo.phase, contentRect);

    if (displayingPoster || !forceSoftwareVideoPaint) {
        // This will display the poster image, if one is present, and otherwise paint nothing.
        ImagePainter(m_layoutVideo).paintIntoRect(context, rect, contentRect);
    } else {
        SkPaint videoPaint = context.fillPaint();
        videoPaint.setColor(SK_ColorBLACK);
        m_layoutVideo.videoElement()->paintCurrentFrame(context.canvas(), pixelSnappedIntRect(rect), &videoPaint);
    }
}

} // namespace blink
