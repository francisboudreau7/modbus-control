#pragma once

namespace mb {

/// Registers mbcore types used in queued signal/slot connections. Call once at
/// startup (app main and tests) before creating a PollWorker.
void registerMetaTypes();

} // namespace mb
