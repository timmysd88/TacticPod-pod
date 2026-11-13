#pragma once
struct MockInternalFS {bool exists(const char*){return true;}};
static MockInternalFS InternalFS;
