/*
Copyright 2014 Mona
mathieu.poux[a]gmail.com
jammetthomas[a]gmail.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License received along this program for more
details (or else see http://www.gnu.org/licenses/).

This file is a part of Mona.
*/

#include "Mona/Peer.h"
#include "Mona/Group.h"
#include "Mona/Handler.h"
#include "Mona/SplitWriter.h"
#include "Mona/ParameterWriter.h"
#include "Mona/Util.h"
#include "Mona/Logs.h"

using namespace std;


namespace Mona {

class Member : public virtual Object {
public:
	Member(Writer* pWriter) : pWriter(pWriter){}
	Writer*			pWriter;
};


Peer::Peer(Handler& handler) : _handler(handler), connected(false) {
}

Peer::~Peer() {
	unsubscribeGroups();
	for(auto& it: _ices) {
		((Peer*)it.first)->_ices.erase(this);
		delete it.second;
	}
}


bool Peer::exchangeMemberId(Group& group,Peer& peer,Writer* pWriter) {
	if (pWriter) {
		pWriter->writeMember(peer);
		return true;
	}
	auto it = peer._groups.find(&group);
	if(it==peer._groups.end()) {
		CRITIC("A peer in a group without have its _groups collection associated")
		return false;
	}
	if(!it->second)
		return false;
	return it->second->writeMember(*this);
}

Group& Peer::joinGroup(const UInt8* id,Writer* pWriter) {
	// create invoker.groups if need
	Group& group(((Entities<Group>&)_handler.groups).create(id));

	// group._clients and this->_groups insertions,
	// before peer id exchange to be able in onJoinGroup to write message BEFORE group join
	auto it = _groups.lower_bound(&group);
	if(it!=_groups.end() && it->first==&group)
		return group;
	_groups.emplace_hint(it,&group,pWriter);
	group.add(*this);
	
	if (pWriter) // if pWriter==NULL it's a dummy member peer
		onJoinGroup(group);
	
	if (group.count() > 1) {
		// If  group includes already members, give 6 last members to the new comer
		UInt8 count=6;
		auto it = group.end();
		do {
			Client& client(*(--it)->second);
			if(client==this->id || !exchangeMemberId(group,(Peer&)client,pWriter))
				continue;
			if(--count==0)
				break;
		} while(it!=group.begin());
	}

	return group;
}


void Peer::unjoinGroup(Group& group) {
	auto it = _groups.lower_bound(&group);
	if (it == _groups.end() || it->first != &group)
		return;
	onUnjoinGroup(*it->first,it->second ? false : true);
	_groups.erase(it);
}

void Peer::unsubscribeGroups(const function<void(const Group& group)>& forEach) {
	for (auto& it : _groups) {
		onUnjoinGroup(*it.first, it.second ? false : true);
		if (forEach)
			forEach(*it.first);
	}
	_groups.clear();
}

ICE& Peer::ice(const Peer& peer) {
	map<const Peer*,ICE*>::iterator it = _ices.begin();
	while(it!=_ices.end()) {
		if(it->first == &peer) {
			it->second->setCurrent(*this);
			return *it->second;
		}
		if(it->second->obsolete()) {
			delete it->second;
			_ices.erase(it++);
			continue;
		}
		if(it->first>&peer)
			break;
		++it;
	}
	if(it!=_ices.begin())
		--it;
	ICE& ice = *_ices.emplace_hint(it,&peer,new ICE(*this,peer,_handler.relayer))->second; // is offer
	((Peer&)peer)._ices[this] = &ice;
	return ice;
}

/// EVENTS ////////


void Peer::onHandshake(UInt32 attempts,set<SocketAddress>& addresses) {
	_handler.onHandshake(protocol, address, path, properties(), attempts, addresses);
}

void Peer::onRendezVousUnknown(const UInt8* peerId,set<SocketAddress>& addresses) {
	_handler.onRendezVousUnknown(protocol, peerId, addresses);
}

void Peer::onConnection(Exception& ex, Writer& writer,DataReader& parameters,DataWriter& response) {
	if(!connected) {
		_pWriter = &writer;

		// reset default protocol parameters
		_parameters.clear();
		Parameters::ForEach forEach([this](const string& key,const string& value) {
			_parameters.setString(key,value);
		});
		string buffer;
		_handler.iterate(String::Format(buffer,protocol,"."), forEach);

		ParameterWriter parameterWriter(_parameters);
		SplitWriter parameterAndResponse(parameterWriter,response);

		_properties.getString("name", (std::string&)name);
		_handler.onConnection(ex, *this,parameters,parameterAndResponse);
		if (!ex) {
			(bool&)connected = ((Clients&)_handler.clients).add(ex,*this);
			if (!connected) {
				if (ex)
					ERROR(ex.error());
				_handler.onDisconnection(*this);
			} else if (ex)
				WARN(ex.error());
		}
		if (!connected) {
			writer.abort();
			_pWriter = NULL;
		} else {
			OnInitParameters::raise(_parameters);
			DEBUG("Client ",address.toString()," connection")
		}
		writer.open(); // open even if "ex" to send error messages!
	} else
		ERROR("Client ", Util::FormatHex(id, ID_SIZE, LOG_BUFFER), " seems already connected!")
}

void Peer::onDisconnection() {
	if (!connected)
		return;
	(bool&)connected = false;
	if (!((Clients&)_handler.clients).remove(*this))
		ERROR("Client ", Util::FormatHex(id, ID_SIZE, LOG_BUFFER), " seems already disconnected!");
	_handler.onDisconnection(*this);
	_pWriter = NULL; // keep after the onDisconnection because otherise the LUA object client.writer can't be deleted!
}

bool Peer::onMessage(Exception& ex, const string& name,DataReader& reader,UInt8 responseType) {
	if(connected)
		return _handler.onMessage(ex, *this, name, reader, responseType);
	ERROR("RPC client before connection")
	return false;
}

void Peer::onJoinGroup(Group& group) {
	if(connected)
		_handler.onJoinGroup(*this,group);
	else
		WARN("onJoinGroup on client not connected")
}

void Peer::onUnjoinGroup(Group& group,bool dummy) {
	// group._clients suppression (this->_groups suppression must be done by the caller of onUnjoinGroup)
	auto itPeer = group.find(id);
	if (itPeer == group.end()) {
		ERROR("onUnjoinGroup on a group which don't know this peer");
		return;
	}
	itPeer = group.remove(itPeer);

	if (!dummy) {
		if(connected)
			_handler.onUnjoinGroup(*this,group);
		else
			WARN("onUnjoinGroup on client not connected")
	}

	if (group.count() == 0) {
		((Entities<Group>&)_handler.groups).erase(group.id);
		return;
	}
	if (itPeer == group.end())
		return;

	// if a peer disconnects of one group, give to its following peer the 6th preceding peer
	Peer& followingPeer((Peer&)*itPeer->second);
	UInt8 count=6;
	while(--count!=0 && itPeer!=group.begin())
		--itPeer;
	if(count==0)
		((Peer*)itPeer->second)->exchangeMemberId(group,followingPeer,NULL);
}

bool Peer::onPublish(const Publication& publication,string& error) {
	if(connected)
		return _handler.onPublish(*this,publication,error);
	WARN("Publication client before connection")
	error = "Client must be connected before publication";
	return false;
}

void Peer::onUnpublish(const Publication& publication) {
	if(connected) {
		_handler.onUnpublish(*this,publication);
		return;
	}
	WARN("Unpublication client before connection")
}

bool Peer::onSubscribe(const Listener& listener,string& error) {
	if(connected)
		return _handler.onSubscribe(*this,listener,error);
	WARN("Subscription client before connection")
	error = "Client must be connected before subscription";
	return false;
}

void Peer::onUnsubscribe(const Listener& listener) {
	if(connected) {
		_handler.onUnsubscribe(*this,listener);
		return;
	}
	WARN("Unsubscription client before connection")
}

bool Peer::onFileAccess(Exception& ex, FileAccessType type, Path& filePath,DataReader& parameters,DataWriter& properties) {
	string relativePath(filePath.fullPath());
	if(connected)
		return _handler.onFileAccess(ex,*this, type, filePath, parameters, properties);
	ERROR("File '", relativePath, "' access by a not connected client")
	return false;
}

void Peer::onDataPacket(const Publication& publication,DataReader& packet) {
	if(connected) {
		_handler.onDataPacket(*this,publication,packet);
		return;
	}
	WARN("DataPacket client before connection")
}

void Peer::onAudioPacket(const Publication& publication,UInt32 time,PacketReader& packet) {
	if(connected) {
		_handler.onAudioPacket(*this,publication,time,packet);
		return;
	}
	WARN("AudioPacket client before connection")
}

void Peer::onVideoPacket(const Publication& publication,UInt32 time,PacketReader& packet) {
	if(connected) {
		_handler.onVideoPacket(*this,publication,time,packet);
		return;
	}
	WARN("VideoPacket client before connection")
}

void Peer::onFlushPackets(const Publication& publication) {
	if(connected) {
		_handler.onFlushPackets(*this,publication);
		return;
	}
	WARN("FlushPackets client before connection")
}





} // namespace Mona
