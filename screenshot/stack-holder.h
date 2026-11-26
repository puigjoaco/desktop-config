#pragma once
#include "clipboard-stack.h"

// An independent transient user service keeps raw MIME data alive across daemon restarts.
void handoffClipboard(const ClipboardStack::Item &item);
void cleanupClipboardHolders();
int holdClipboard(int argc, char **argv, const QString &path);
