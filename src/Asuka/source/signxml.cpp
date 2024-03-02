//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <vd2/system/vdstl.h>
#include <stdio.h>
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

void tool_signxml(const vdfastvector<const char *>& args, const vdfastvector<const char *>& switches) {
	if (args.size() < 3) {
		printf("Usage: signxml cert-name input.xml output.xml\n");
		exit(5);
	}

	HCERTSTORE hcs = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"My");
	if (!hcs) {
		printf("Error: Failed to open certificate store.\n");
		exit(10);
	}

	PCCERT_CONTEXT cert = CertFindCertificateInStore(hcs, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_STR_A, args[0], nullptr);
	if (!cert) {
		printf("Error: Unable to find certificate: %s\n", args[0]);
		exit(10);
	}

	NCRYPT_KEY_HANDLE okey {};
	BOOL callerFrees = FALSE;
	DWORD keySpec = 0;
	if (!CryptAcquireCertificatePrivateKey(cert, CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG, nullptr, &okey, &keySpec, &callerFrees)) {
		printf("Error: Unable to acquire private key.\n");
		exit(10);
	}

	FILE *f = fopen(args[1], "rb");
	if (!f) {
		printf("Error: Unable to open input file: %s\n", args[1]);
		exit(10);
	}
		
	long len = 0;
	if (fseek(f, 0, SEEK_END) < 0 || (len = ftell(f)) < 0) {
		printf("Error: Unable to get size of input file: %s\n", args[1]);
		exit(10);
	}

	std::vector<char> xmlbuf(len);

	if (fseek(f, 0, SEEK_SET) < 0 || 1 != fread(xmlbuf.data(), len, 1, f)) {
		printf("Error: Unable to read input file: %s\n", args[1]);
		exit(10);
	}

	auto it = std::find(xmlbuf.begin(), xmlbuf.end(), '>');
	if (it == xmlbuf.end()) {
		printf("Error: Unable to find insertion point in source XML file.\n");
		exit(10);
	}

	++it;

	// we need to insert: <!-- sig:(base64 len 344) -->
	it = xmlbuf.insert(it, 344 + 13, ' ');

	// compute SHA256 hash
	UCHAR sha256hash[32];
	BCRYPT_ALG_HANDLE sha256 {};
	BCRYPT_HASH_HANDLE hash {};

	if (FAILED(BCryptOpenAlgorithmProvider(&sha256, BCRYPT_SHA256_ALGORITHM, MS_PRIMITIVE_PROVIDER, 0))
		|| FAILED(BCryptCreateHash(sha256, &hash, nullptr, 0, nullptr, 0, 0))
		|| FAILED(BCryptHashData(hash, (PUCHAR)xmlbuf.data(), (ULONG)xmlbuf.size(), 0))
		|| FAILED(BCryptFinishHash(hash, (PUCHAR)sha256hash, sizeof sha256hash, 0)))
	{
		printf("Error: Unable to compute SHA256 hash.\n");
		exit(10);
	}

	BCryptDestroyHash(hash);
	BCryptCloseAlgorithmProvider(sha256, 0);

	BCRYPT_PSS_PADDING_INFO pssPad {};
	pssPad.pszAlgId = BCRYPT_SHA256_ALGORITHM;
	pssPad.cbSalt = 8;
	DWORD sigLen = 0;
	BYTE signature[256 + 2] {};		// +2 to make base64 easier
	if (FAILED(NCryptSignHash(okey, &pssPad, sha256hash, sizeof sha256hash, signature, 256, &sigLen, BCRYPT_PAD_PSS))
		|| sigLen != 256)
	{
		printf("Error: Unable to create signature.\n");
		exit(10);
	}

	// convert hash to base64
	char *dst = &*it;

	memcpy(dst, "<!-- sig:", 9);
	dst += 9;

	// 256 bytes converts to 344 base64 chars with two pad chars
	static constexpr struct Base64Enc {
		char tab[64];

		constexpr Base64Enc()
			: tab()
		{
			for(int i=0; i<26; ++i) {
				tab[i] = 'A' + i;
				tab[i + 26] = 'a' + i;
			}

			for(int i=0; i<10; ++i)
				tab[i + 52] = '0' + i;

			tab[62] = '+';
			tab[63] = '/';
		}
	} kBase64Enc {};

	const BYTE *src = signature;
	for(int i = 0; i < 86; ++i) {
		uint8_t v0 = src[0];
		uint8_t v1 = src[1];
		uint8_t v2 = src[2];
		src += 3;

		dst[0] = kBase64Enc.tab[v0 >> 2];
		dst[1] = kBase64Enc.tab[((v0 << 4) & 0x30) + (v1 >> 4)];
		dst[2] = kBase64Enc.tab[((v1 << 2) & 0x3C) + (v2 >> 6)];
		dst[3] = kBase64Enc.tab[v2 & 0x3F];
		dst += 4;
	}

	memcpy(dst - 2, "== -->", 6);

	FILE *fo = fopen(args[2], "wb");
	if (!fo) {
		printf("Error: Unable to open output file: %s\n", args[2]);
		exit(10);
	}

	if (1 != fwrite(xmlbuf.data(), xmlbuf.size(), 1, fo) || fclose(fo)) {
		printf("Error: Unable to write output file: %s\n", args[2]);
		exit(10);
	}

	fclose(f);
}
