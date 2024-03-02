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

void tool_signexport(const vdfastvector<const char *>& args, const vdfastvector<const char *>& switches) {
	if (args.size() < 1) {
		printf("Usage: signexport cert-name\n");
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

	ULONG pkeyLen = 0;
	if (FAILED(NCryptExportKey(okey, {}, BCRYPT_RSAPUBLIC_BLOB, nullptr, nullptr, 0, &pkeyLen, 0))) {
		printf("Error: Unable to get size of public key.\n");
		exit(10);
	}

	std::unique_ptr<UCHAR[]> publicKey(new UCHAR[pkeyLen]);
	if (FAILED(NCryptExportKey(okey, {}, BCRYPT_RSAPUBLIC_BLOB, nullptr, publicKey.get(), pkeyLen, &pkeyLen, 0))) {
		printf("Error: Unable to read public key.\n");
		exit(10);
	}

	for(uint32_t i = 0; i < pkeyLen; i += 16) {
		uint32_t rowLen = std::min<uint32_t>(pkeyLen - i, 16);

		for(uint32_t j = 0; j < rowLen; ++j) {
			printf("0x%02X,", publicKey[i + j]);
		}

		putchar('\n');
	}
}
