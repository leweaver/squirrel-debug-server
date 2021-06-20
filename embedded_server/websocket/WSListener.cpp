#include "WSListener.h"

#include "oatpp/core/macro/component.hpp"

using sdb::RemoteConnection;
using sdb::WSInstanceListener;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// RemoteConnection

RemoteConnection::RemoteConnection(const WebSocket& webSocket)
    : webSocket_(webSocket) {}

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

  } else if(size > 0) { // message frame received
    messageBuffer_.writeSimple(data, size);
  }
}

void RemoteConnection::sendMessage(const oatpp::String& message) {
  webSocket_.sendOneFrameText(message);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WSInstanceListener

std::atomic<v_int32> WSInstanceListener::SOCKETS(0);

void WSInstanceListener::broadcastMessage(const oatpp::String& message) {
  OATPP_LOGD(TAG, "Broadcasting to %d clients: %s",
             static_cast<uint32_t>(connections_.size()),
             message->c_str());
  for (const auto& connection : connections_) {
    connection->sendMessage(message);
  }
}

void WSInstanceListener::onAfterCreate(const oatpp::websocket::WebSocket& socket, const std::shared_ptr<const ParameterMap>& params) {

  auto oldCount = SOCKETS.fetch_add(1);
  OATPP_LOGD(TAG, "New Incoming Connection. Connection count=%d", oldCount + 1);

  auto remoteConnection = std::make_shared<RemoteConnection>(socket);
  socket.setListener(remoteConnection);

  connections_.push_back(remoteConnection);
}

void WSInstanceListener::onBeforeDestroy(const oatpp::websocket::WebSocket& socket) {
  const auto listener = socket.getListener();

  const auto pos = std::find_if(connections_.begin(), connections_.end(), [listener = listener.get()](const auto& conn) { return conn.get() == listener; });
  if (pos == connections_.end()) {
    OATPP_LOGE(TAG, "Failed to remove connection.");
  } else {
    connections_.erase(pos);
  }

  auto oldCount = SOCKETS.fetch_add(-1);
  OATPP_LOGD(TAG, "Connection closed. Connection count=%d", oldCount - 1);
}