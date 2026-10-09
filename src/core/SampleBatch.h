#pragma once

#include "Tag.h"

#include <QList>
#include <QMetaType>

namespace mb {

/// One tag's result within a poll cycle. raw/value are only meaningful when
/// quality == Quality::Good.
struct TagSample {
    TagId tagId = 0;
    quint32 raw = 0;    ///< combineRegisters() result (16-bit types use the low 16 bits)
    double value = 0.0; ///< scaled value (see decodeValue)
    Quality quality = Quality::Unknown;
};

/// All samples of one poll cycle; they share one timestamp.
struct SampleBatch {
    double t = 0.0; ///< seconds since the session time base (see PollWorker::timeBaseReset)
    QList<TagSample> samples;
};

} // namespace mb

Q_DECLARE_METATYPE(mb::TagSample)
Q_DECLARE_METATYPE(mb::SampleBatch)
