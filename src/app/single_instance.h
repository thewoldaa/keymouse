// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace km {

// Single instance enforcement plus a small command channel.
//
// A second launch must not install a second keyboard hook: two hooks would
// both swallow keys and the effects would double. Instead the second process
// signals the first to show its dashboard and exits.
namespace single_instance {

// Creates the mutex and starts the message listener. Returns false when
// another instance already owns the mutex.
bool acquire();

// Sends a command to an already running instance. Commands are single words:
// "show", "toggle", "reload", "exit".
bool send_command(const std::string& command);

// Invoked on the instance thread when a command arrives. Called from the
// listener thread; the application forwards it to the UI thread.
void set_command_handler(void (*handler)(const std::string& command));

// Stops the listener and releases the mutex.
void release();

}  // namespace single_instance

}  // namespace km
