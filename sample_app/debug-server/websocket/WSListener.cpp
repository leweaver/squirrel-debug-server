#include "WSListener.h"

#include "oatpp/core/macro/component.hpp"

using namespace qdb;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// RemoteConnection

void RemoteConnection::onPing(const WebSocket& socket, const oatpp::String& message) {
  OATPP_LOGD(TAG, "onPing");
  socket.sendPong(message);
}

void RemoteConnection::onPong(const WebSocket& socket, const oatpp::String& message) {
  OATPP_LOGD(TAG, "onPong");
}

void RemoteConnection::onClose(const WebSocket& socket, v_uint16 code, const oatpp::String& message) {
  OATPP_LOGD(TAG, "onClose code=%d", code);
}

void RemoteConnection::readMessage(const WebSocket& socket, v_uint8 opcode, p_char8 data, oatpp::v_io_size size) {

  if(size == 0) { // message transfer finished

    auto wholeMessage = messageBuffer_.toString();
    messageBuffer_.clear();

    OATPP_LOGD(TAG, "onMessage message='%s'", wholeMessage->c_str());
    handleCommandMessage(socket, wholeMessage);

  } else if(size > 0) { // message frame received
    messageBuffer_.writeSimple(data, size);
  }

}

void RemoteConnection::handleCommandMessage(const WebSocket& socket, const oatpp::String& message) {
  if (message == "pause") {
    commandInterface_->Pause();
  } else if (message == "play") {
    commandInterface_->Play();
  } else {
    socket.sendOneFrameText("err: unknown command " + message);
    return;
  }

  /* Send message in reply */
  socket.sendOneFrameText("ack: " + message);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WSInstanceListener

std::atomic<v_int32> WSInstanceListener::SOCKETS(0);

void WSInstanceListener::onAfterCreate(const oatpp::websocket::WebSocket& socket, const std::shared_ptr<const ParameterMap>& params) {

  auto newCount = SOCKETS.fetch_add(1);
  OATPP_LOGD(TAG, "New Incoming Connection. Connection count=%d", newCount);

  /* In this particular case we create one RemoteConnection per each connection */
  /* Which may be redundant in many cases */
  socket.setListener(std::make_shared<RemoteConnection>(commandInterface_));
}

void WSInstanceListener::onBeforeDestroy(const oatpp::websocket::WebSocket& socket) {

  auto newCount = SOCKETS.fetch_add(-1);
  OATPP_LOGD(TAG, "Connection closed. Connection count=%d", newCount);

}