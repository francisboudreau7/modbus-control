#include "MetaTypes.h"

#include "ModbusConnection.h"
#include "PollWorker.h"
#include "SampleBatch.h"

#include <QMetaType>

namespace mb {

void registerMetaTypes()
{
    qRegisterMetaType<mb::TagSample>();
    qRegisterMetaType<mb::SampleBatch>();
    qRegisterMetaType<mb::ConnectionSettings>();
    qRegisterMetaType<mb::ModbusResult>();
    qRegisterMetaType<mb::ConnectionState>();
    qRegisterMetaType<mb::Tag>();
    qRegisterMetaType<mb::Area>();
    qRegisterMetaType<QList<mb::Tag>>();
    qRegisterMetaType<QList<quint16>>();
}

} // namespace mb
