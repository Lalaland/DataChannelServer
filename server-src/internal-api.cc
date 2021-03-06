#include "DataChannelServer/server-src/internal-api.h"

#include "webrtc/api/peerconnectioninterface.h"
#include "webrtc/api/test/fakeconstraints.h"
#include "webrtc/base/json.h"

#include <cstdio>

struct ProcessingThread {
  std::unique_ptr<rtc::Thread> thread;
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
};

ProcessingThread* CreateProcessingThread() {
  ProcessingThread* result = new ProcessingThread();
  result->thread = rtc::Thread::Create();
  result->thread->Start();

  result->thread->Invoke<bool>(RTC_FROM_HERE, [result]() {
    result->factory = webrtc::CreatePeerConnectionFactory();

    return true;
  });

  return result;
}

class PeerConnectionObserverWrapper {
public:
  PeerConnectionObserverWrapper(PeerConnectionObserver observer) : observer_(observer) {}

  ~PeerConnectionObserverWrapper() {
    observer_.Deleter(observer_.data);
  }

  void OnOpen() {
    observer_.OnOpen(observer_.data);
  }

  void OnClose() {
    observer_.OnClose(observer_.data);
  }

  void ProcessWebsocketMessage(const char* message, int message_length) {
    observer_.ProcessWebsocketMessage(observer_.data, message, message_length);
  }

  void ProcessDataChannelMessage(const char* message, int message_length) {
    observer_.ProcessDataChannelMessage(observer_.data, message, message_length);
  }

private:
  PeerConnectionObserver observer_;
};


class Foo;
class ChannelThingy;
struct PeerConnection {
public:
  PeerConnection(PeerConnectionObserver observer) : observer_(observer) {}

  PeerConnectionObserverWrapper observer_;

  std::unique_ptr<Foo> f;
  std::unique_ptr<ChannelThingy> channel;
  rtc::scoped_refptr<webrtc::CreateSessionDescriptionObserver> offer_obs;
  rtc::scoped_refptr<webrtc::SetSessionDescriptionObserver> set_local_obs;
  rtc::scoped_refptr<webrtc::SetSessionDescriptionObserver> set_remote_obs;
  rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel;
  rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection;
};

class ChannelThingy : public webrtc::DataChannelObserver {
 public:
  ChannelThingy(PeerConnection* a_peer) : peer(a_peer) {}
 private:
  // The data channel state have changed.
  void OnStateChange() override {
    printf("STATE CHANGE FOR CHANNEL %d\n", peer->data_channel->state());
    if (peer->data_channel->state() == webrtc::DataChannelInterface::kOpen) {
      peer->observer_.OnOpen();
    } else if (peer->data_channel->state() == webrtc::DataChannelInterface::kClosed) {
      peer->observer_.OnClose();
    }
  }

  //  A data buffer was successfully received.
  void OnMessage(const webrtc::DataBuffer& buffer) override {
    printf("GOT MESSAGE %s\n", std::string((char*)buffer.data.data(), buffer.data.size()).c_str());
    peer->observer_.ProcessDataChannelMessage((char*)buffer.data.data(), buffer.data.size());
  }
  // The data channel's buffered_amount has changed.
  void OnBufferedAmountChange(uint64_t previous_amount) override {
    printf("BUFFER AMOUNT CHANGE? WTF? %lu\n", previous_amount);
  }

  PeerConnection* peer;
};

class Foo : public webrtc::PeerConnectionObserver {
 public:
  Foo(PeerConnection* a_peer) : peer(a_peer) {}

 private:
  virtual void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState new_state) {
    printf("Signal\n");
  }

  // Triggered when media is received on a new stream from remote peer.
  virtual void OnAddStream(
      rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
    printf("Add stream\n");
  }

  // Triggered when a remote peer close a stream.
  virtual void OnRemoveStream(
      rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
    printf("Remove stream\n");
  }

  // Triggered when a remote peer opens a data channel.
  virtual void OnDataChannel(
      rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) {
    printf("Data channel\n");
  }

  // Triggered when renegotiation is needed. For example, an ICE restart
  // has begun.
  virtual void OnRenegotiationNeeded() { printf("On renegotiion\n"); }

  // Called any time the IceConnectionState changes.
  virtual void OnIceConnectionChange(
      webrtc::PeerConnectionInterface::IceConnectionState new_state) {
    printf("ICE\n");
  }

  // Called any time the IceGatheringState changes.
  virtual void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState new_state) {
    printf("ICE Gathering\n");
  }

  // A new ICE candidate has been gathered.
  virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
    printf("ICE Candidate\n");
    std::string candidate_str;
    ;
    if (!candidate->ToString(&candidate_str)) {
      printf("FAILED TO STRING ICE\n");
    }

    Json::Value root;
    root["type"] = "icecandidate";

    Json::Value ice;
    ice["candidate"] = candidate_str;
    ice["sdpMid"] = candidate->sdp_mid();
    ice["sdpMLineIndex"] = candidate->sdp_mline_index();

    root["ice"] = ice;

    std::string message = rtc::JsonValueToString(root);

    peer->observer_.ProcessWebsocketMessage(message.data(), message.size());
  }

  // Ice candidates have been removed.
  virtual void OnIceCandidatesRemoved(
      const std::vector<cricket::Candidate>& candidates) {
    printf("ICE remove\n");
  }

  // Called when the ICE connection receiving status changes.
  virtual void OnIceConnectionReceivingChange(bool receiving) {
    printf("Ice connection change\n");
  }

 private:
  PeerConnection* peer;
};

class SetLocalOfferObserver : public webrtc::SetSessionDescriptionObserver {
 public:
  SetLocalOfferObserver(PeerConnection* a_peer, Json::Value a_sdi)
      : peer(a_peer), sdi(std::move(a_sdi)) {}

  void OnSuccess() override {
    Json::Value root;
    root["type"] = "offer";
    root["sdi"] = sdi;

    std::string message = rtc::JsonValueToString(root);

    peer->observer_.ProcessWebsocketMessage(message.data(), message.size());
  }
  void OnFailure(const std::string& error) {
    printf("FAILFAILFAILFAIL SET LOCAL OFFER %s\n", error.c_str());
  }

 private:
  PeerConnection* peer;
  Json::Value sdi;
};

class SetRemoteOfferObserver : public webrtc::SetSessionDescriptionObserver {
 public:
  SetRemoteOfferObserver(PeerConnection* a_peer) : peer(a_peer) {}

  void OnSuccess() override {
    printf("I AM GOOD TO GO!!!!\n");
    (void)peer;
  }
  void OnFailure(const std::string& error) {
    printf("FAILFAILFAILFAIL SET REMOTE OFFER %s\n", error.c_str());
  }

 private:
  PeerConnection* peer;
};

class CreateOfferObserver : public webrtc::CreateSessionDescriptionObserver {
 public:
  CreateOfferObserver(PeerConnection* a_peer) : peer(a_peer) {}

  void OnSuccess(webrtc::SessionDescriptionInterface* sdi) override {
    std::string sdp;
    if (!sdi->ToString(&sdp)) {
      printf("FAIL SERIALIZE THINGY \n");
    }

    Json::Value sdi_obj;
    sdi_obj["type"] = sdi->type();
    sdi_obj["sdp"] = sdp;

    peer->set_local_obs =
        new rtc::RefCountedObject<SetLocalOfferObserver>(peer, sdi_obj);
    peer->peer_connection->SetLocalDescription(peer->set_local_obs, sdi);
  }
  void OnFailure(const std::string& error) override {
    printf("FAILFAILFAILFAIL CREATE OFFER %s\n", error.c_str());
  }

 private:
  PeerConnection* peer;
};

EXPORT PeerConnection* CreatePeerConnection(
    ProcessingThread* thread,
    PeerConnectionObserver observer,
    DataChannelOptions options) {
  return thread->thread->Invoke<PeerConnection*>(
      RTC_FROM_HERE,
      [thread, observer, options]() {
        PeerConnection* peer = new PeerConnection(observer);

        peer->channel = std::unique_ptr<ChannelThingy>(new ChannelThingy(peer));

        webrtc::PeerConnectionInterface::RTCConfiguration config;
        webrtc::PeerConnectionInterface::IceServer ice_server;
        ice_server.uri = "stun:stun.l.google.com:19302";
        config.servers.push_back(ice_server);

        webrtc::FakeConstraints constraints;
        constraints.AddOptional(
            webrtc::MediaConstraintsInterface::kEnableDtlsSrtp, "true");

        peer->f = std::unique_ptr<Foo>(new Foo(peer));
        peer->peer_connection = thread->factory->CreatePeerConnection(
            config, &constraints, nullptr, nullptr, peer->f.get());

        webrtc::DataChannelInit data_channel_config;
        data_channel_config.ordered = options.ordered;
        data_channel_config.maxRetransmitTime = options.maxRetransmitTime;
        data_channel_config.maxRetransmits = options.maxRetransmits;

        peer->data_channel =
            peer->peer_connection->CreateDataChannel("lolchannel", &data_channel_config);
        peer->data_channel->RegisterObserver(peer->channel.get());

        peer->offer_obs = new rtc::RefCountedObject<CreateOfferObserver>(peer);

        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;

        peer->peer_connection->CreateOffer(peer->offer_obs, options);

        return peer;
      });
}

void DeletePeerConnection(ProcessingThread* thread, PeerConnection* peer) {
  thread->thread->Invoke<bool>(RTC_FROM_HERE, [peer]() {
    delete peer;
    return true;
  });
}

void DeleteProcessingThread(ProcessingThread* thread) {
  thread->thread->Stop();
  delete thread;
}

template <typename F>
struct OnceFunctorHelper : rtc::MessageHandler {
  OnceFunctorHelper(F functor) : functor_(functor) {}

  void OnMessage(rtc::Message* /*msg*/) override {
    functor_();
    delete this;
  }

  F functor_;
};

template <typename F>
rtc::MessageHandler* OnceFunctor(F functor) {
  return new OnceFunctorHelper<F>(functor);
}

void ProcessAnswer(PeerConnection* peer, const Json::Value& sdi_obj) {
  std::string sdp_str = sdi_obj["sdp"].asString();
  std::string type_str = sdi_obj["type"].asString();

  webrtc::SessionDescriptionInterface* sdi =
      webrtc::CreateSessionDescription(type_str, sdp_str, nullptr);

  if (sdi == nullptr) {
    printf("THE SDI IS NULL???? WHYWHYWHYW %s\n",
           rtc::JsonValueToString(sdi_obj).c_str());
  }

  peer->set_remote_obs =
      new rtc::RefCountedObject<SetRemoteOfferObserver>(peer);
  peer->peer_connection->SetRemoteDescription(peer->set_remote_obs, sdi);
}

void ProcessIce(PeerConnection* peer, const Json::Value& ice_obj) {
  std::string candidate_str = ice_obj["candidate"].asString();
  std::string sdp_mid_str = ice_obj["sdpMid"].asString();
  int sdp_mline_index = ice_obj["sdpMLineIndex"].asInt();

  webrtc::IceCandidateInterface* ice = webrtc::CreateIceCandidate(
      sdp_mid_str, sdp_mline_index, candidate_str, nullptr);

  if (ice == nullptr) {
    printf("THE ICE IS NULL???? WHYWHYWHYW %s\n",
           rtc::JsonValueToString(ice_obj).c_str());
  }

  peer->peer_connection->AddIceCandidate(ice);
  delete ice;
}

void ProcessWebsocketMessage(PeerConnection* peer, const std::string& message) {
  Json::Value root;
  Json::Reader reader;
  if (!reader.parse(message, root)) {
    printf("FAILED TO PARSE%s \n", message.c_str());
  }

  std::string type = root["type"].asString();

  if (type == "answer") {
    ProcessAnswer(peer, root["sdi"]);
  } else if (type == "icecandidate") {
    ProcessIce(peer, root["ice"]);
  } else {
    printf("INVALID TYPE %s \n", message.c_str());
  }
}

void SendWebsocketMessage(ProcessingThread* thread,
                        PeerConnection* peer,
                        const char* message,
                        int message_length) {
  std::string data(message, message_length);

  auto handle =
      OnceFunctor([peer, data]() { ProcessWebsocketMessage(peer, data); });

  thread->thread->Post(RTC_FROM_HERE, handle);
}

void ProcessDataChannelMessage(PeerConnection* peer,
                               const std::string& message) {
  if (!peer->data_channel->Send(webrtc::DataBuffer(message))) {
    printf("THE SEND FOR DATA CHANNEL DIDN'T WORK PROPERLY!\n");
  }
}

void SendDataChannelMessage(ProcessingThread* thread,
                          PeerConnection* peer,
                          const char* message,
                          int message_length) {
  std::string data(message, message_length);

  auto handle =
      OnceFunctor([peer, data]() { ProcessDataChannelMessage(peer, data); });

  thread->thread->Post(RTC_FROM_HERE, handle);
}
