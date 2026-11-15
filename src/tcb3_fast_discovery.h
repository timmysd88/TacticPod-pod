#pragma once
#include "tcb3_store.h"

namespace Tcb3FastDiscovery {
enum class Result : uint8_t { Scanning, Complete, Incomplete, Corrupt, Ambiguous, Overflow, IoError, NotStarted };
struct Candidate {
  Tcb3::ManifestEntry metadata;
  bool provisional;
};

class Scanner {
public:
  void begin(Tcb3::Port port, uint32_t end);
  // The caller must own an awake, idle flash, grant its service budget and restart after mutation.
  void update(bool allow);
  Result result() const { return outcome; }
  bool done() const { return outcome != Result::Scanning; }
  unsigned count() const { return usable() ? entryCount : 0; }
  uint32_t incomplete() const { return incompleteCount; }
  bool overflow() const { return tooMany; }
  const Candidate* entry(unsigned index) const { return usable() && index < entryCount ? entries + index : nullptr; }
  const char* error() const { return problem; }
private:
  enum class Phase : uint8_t { Marker, CleanPrefix, Probe, Header, Footer };
  Tcb3::Port port{};
  Candidate entries[Tcb3::MaxManifestSessions]{}, current{};
  uint8_t page[Tcb3::Page]{}, metadata[Tcb3::MetadataBytes]{};
  uint32_t address=0, limit=0, epoch=0, sectorSequence=0, pageSequence=0;
  uint32_t incompleteCount=0;
  unsigned entryCount=0, offset=0;
  bool sessionOpen=false, tooMany=false;
  Phase phase=Phase::Marker;
  Result outcome=Result::NotStarted;
  const char* problem=nullptr;
  bool usable() const { return outcome==Result::Complete || outcome==Result::Incomplete; }
  bool read(uint32_t address, uint8_t* output, unsigned size);
  bool readPage(uint32_t address);
  void fail(Result result, const char* message);
  void finish();
  void next();
  void header();
  void footer();
};
}
