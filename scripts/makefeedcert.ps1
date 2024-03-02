# Altirra update feed certificate generator PowerShell script
#
# This generates a new self-signed certificate with the subject 'AltirraTestUpdate'; it
# should be renamed to CN=AltirraUpdate to be compatible with the feed scripts. You can
# also use another way of generating a certificate as long as it uses 2048-bit RSA
# with SHA256 hashing and can be imported into the Windows certificate store for
# signing. The update check in the program uses a hardcoded RSA public key and there is
# intentionally no certificate chain or expiration checking.
#
# As this generates a new certificate, it will not be compatible with existing versions
# of Altirra and is only useful for regenerating a new, incompatible feed. The RSA
# public key is hardcoded in updatefeed.cpp and must be replaced with the output of
# 'asuka signexport' for the new certificate.

$cert = New-SelfSignedCertificate -certstorelocation cert:\currentuser\my -Type Custom -Subject "CN=AltirraTestUpdate" -FriendlyName "AltirraTestUpdate" -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(20) -KeyExportPolicy Exportable
