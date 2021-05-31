//
// Created by Lewis weaver on 5/31/2021.
//
#pragma once

#ifndef SAMPLE_APP_MESSAGE_INTERFACE_H
#define SAMPLE_APP_MESSAGE_INTERFACE_H

namespace qdb {
/// <summary>
/// Interface that is used to communicate commands from the remote debugger
/// Implemention is provided by the application.
/// </summary>
class MessageCommandInterface {
 public:
  /// <summary>
  /// Instructs the program to pause execution at its current point
  /// </summary>
  virtual void Pause() = 0;

  /// <summary>
  /// Instructs the program to resume execution if it was previously paused
  /// </summary>
  virtual void Play() = 0;
};

/// <summary>
/// Interface that is used to communicate state from the app to the remote debugger.
/// Implemention is provided by the remote debugger implementation.
/// </summary>
class MessageEventInterface {
 public:
  virtual void OnBreakpointHit() = 0;
};
}

#endif
