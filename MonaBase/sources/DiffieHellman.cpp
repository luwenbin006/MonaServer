/*
 *  Copyright (c) 2010,
 *  Gavriloaie Eugen-Andrei (shiretu@gmail.com)
 *
 *  This file is part of crtmpserver.
 *  crtmpserver is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  crtmpserver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with crtmpserver.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "Mona/DiffieHellman.h"
#include "Mona/Logs.h"

using namespace std;
using namespace Poco;

namespace Mona {

UInt8 DH1024p[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xC9, 0x0F, 0xDA, 0xA2, 0x21, 0x68, 0xC2, 0x34,
	0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1,
	0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74,
	0x02, 0x0B, 0xBE, 0xA6, 0x3B, 0x13, 0x9B, 0x22,
	0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
	0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B,
	0x30, 0x2B, 0x0A, 0x6D, 0xF2, 0x5F, 0x14, 0x37,
	0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45,
	0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6,
	0xF4, 0x4C, 0x42, 0xE9, 0xA6, 0x37, 0xED, 0x6B,
	0x0B, 0xFF, 0x5C, 0xB6, 0xF4, 0x06, 0xB7, 0xED,
	0xEE, 0x38, 0x6B, 0xFB, 0x5A, 0x89, 0x9F, 0xA5,
	0xAE, 0x9F, 0x24, 0x11, 0x7C, 0x4B, 0x1F, 0xE6,
	0x49, 0x28, 0x66, 0x51, 0xEC, 0xE6, 0x53, 0x81,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};


DiffieHellman::DiffieHellman() : _pDH(NULL) {
	
}

DiffieHellman::~DiffieHellman() {
	if(_pDH)
		DH_free(_pDH);
}

bool DiffieHellman::initialize(bool reset) {
	if(!reset && _pDH)
		return false;
	if(_pDH)
		DH_free(_pDH);
	_pDH = DH_new();
	_pDH->p = BN_new();
	_pDH->g = BN_new();

	//3. initialize p, g and key length
	BN_set_word(_pDH->g, 2); //group DH 2
	BN_bin2bn(DH1024p,DH_KEY_SIZE,_pDH->p); //prime number

	//4. Generate private and public key
	if(!DH_generate_key(_pDH))
		CRITIC("Generation DH key failed!");
	return true;
}


void DiffieHellman::computeSecret(const Buffer<UInt8>& farPubKey,Buffer<UInt8>& sharedSecret) {
	initialize();
	BIGNUM *bnFarPubKey = BN_bin2bn(&farPubKey[0],farPubKey.size(),NULL);
	int i =BN_num_bits(_pDH->priv_key);
	sharedSecret.resize(DH_KEY_SIZE);
	int size = DH_compute_key(&sharedSecret[0], bnFarPubKey, _pDH);
	if (size <= 0)
		CRITIC("Diffie Hellman exchange failed, dh compute key error")
	else if(size!=DH_KEY_SIZE)
		sharedSecret.resize(size);
	BN_free(bnFarPubKey);
}



}  // namespace Mona