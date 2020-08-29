# Symantec Certificates

This directory contains the set of known active and legacy root certificates
that were operated by Symantec Corporation. In order for certificates issued
from these roots to be trusted, it is required that they comply with the
policies outlined at <https://security.googleblog.com/2017/09/chromes-plan-to-distrust-symantec.html>.

The exceptions to this are:
  * Pre-existing independently operated sub-CAs, whose keys were and are not
    controled by Symantec and which maintain current and appropriate audits.
  * The set of Managed CAs in accordance with the above policies.

In addition to the above, no changes exist from the Certificate Transparency
requirement outlined at <https://security.googleblog.com/2015/10/sustaining-digital-certificate-security.html>

## Implementation Details

Policies related to these certificates are based on the hash of the
subjectPublicKeyInfo, rather than of the certificate, and without considering
the Subject Distinguished Name.

The choice of using subjectPublicKeyInfo is two-fold:

* If there are any concerns with the how the key material has been protected,
  those concerns apply to all subject names, not just the known subject names.
  By limiting trust in the SPKI, the underlying issue is addressed. This also
  helps address any concerns with potential cross-signs in the future, as has
  been seen in past CA remediation efforts.
* Simultaneously, if there are no concerns with the SPKI, such as due to being
  on the exclusions list, then we want to ensure ecosystem flexibility in the
  event that the certificates themselves need to be reissued. The most likely
  cause for reissusance of Excluded Sub-CAs may be presumed to be either
  expiration or due to wanting to add additional extensions (such as to reduce
  the scope of issuance). To avoid unduly limiting the ecosystem flexibility
  in the event of those changes, excluding by SPKI allows for some limited
  agility, while being grounded in the objective evaluation of the key and how
  the key material has been operated and protected. In the context of Managed
  CAs, this ensures that additional (effectively cross-signed) versions of the
  Managed Partner Infrastructure can be introduced as needed, while ensuring no
  additional code changes or updates are necessary.

Thus, identifying 'roots' (which may appear anywhere in the chain) by SPKI help
ensure the appropriate restrictions are applied, regardless of cross-signs or
self-signed variations, while identifying 'exclusions' by SPKI helps ensure the
necessary flexibility to respond to ecosystem changes.

## Roots

The full set of roots are in the [roots/](roots/) directory, organized by
SHA-256 hash of the certificate file.

The following command can be used to match certificates and their key hashes:

`` for f in roots/*.pem; do openssl x509 -noout -pubkey -in "${f}" | openssl asn1parse -inform pem -out /tmp/pubkey.out -noout; digest=`cat /tmp/pubkey.out | openssl dgst -sha256 -c | awk -F " " '{print $2}' | sed s/:/,0x/g `; echo "0x${digest} ${f##*/}"; done | sort ``

## Excluded Sub-CAs

### Apple

[WebTrust Audit](https://cert.webtrust.org/ViewSeal?id=1917)
[Certification Practices Statement](http://images.apple.com/certificateauthority/pdf/Apple_IST_CPS_v2.0.pdf)

  * [17f96609ac6ad0a2d6ab0a21b2d1b5b2946bd04dbf120703d1def6fb62f4b661.pem](excluded/17f96609ac6ad0a2d6ab0a21b2d1b5b2946bd04dbf120703d1def6fb62f4b661.pem)
  * [3db76d1dd7d3a759dccc3f8fa7f68675c080cb095e4881063a6b850fdd68b8bc.pem](excluded/3db76d1dd7d3a759dccc3f8fa7f68675c080cb095e4881063a6b850fdd68b8bc.pem)
  * [6115f06a338a649e61585210e76f2ece3989bca65a62b066040cd7c5f408edd0.pem](excluded/6115f06a338a649e61585210e76f2ece3989bca65a62b066040cd7c5f408edd0.pem)
  * [904fb5a437754b1b32b80ebae7416db63d05f56a9939720b7c8e3dcc54f6a3d1.pem](excluded/904fb5a437754b1b32b80ebae7416db63d05f56a9939720b7c8e3dcc54f6a3d1.pem)
  * [ac2b922ecfd5e01711772fea8ed372de9d1e2245fce3f57a9cdbec77296a424b.pem](excluded/ac2b922ecfd5e01711772fea8ed372de9d1e2245fce3f57a9cdbec77296a424b.pem)
  * [a4fe7c7f15155f3f0aef7aaa83cf6e06deb97ca3f909df920ac1490882d488ed.pem](excluded/a4fe7c7f15155f3f0aef7aaa83cf6e06deb97ca3f909df920ac1490882d488ed.pem)

### DigiCert

[WebTrust Audit](https://cert.webtrust.org/ViewSeal?id=2228)
[Certification Practices Statement](https://www.digicert.com/CPS)

  * [8bb593a93be1d0e8a822bb887c547890c3e706aad2dab76254f97fb36b82fc26.pem](excluded/8bb593a93be1d0e8a822bb887c547890c3e706aad2dab76254f97fb36b82fc26.pem)
  * [b94c198300cec5c057ad0727b70bbe91816992256439a7b32f4598119dda9c97.pem](excluded/b94c198300cec5c057ad0727b70bbe91816992256439a7b32f4598119dda9c97.pem)

### Google

[WebTrust Audit](https://cert.webtrust.org/ViewSeal?id=1941)
[Certification Practices Statement](http://static.googleusercontent.com/media/pki.google.com/en//GIAG2-CPS-1.3.pdf)

  * [c3f697a92a293d86f9a3ee7ccb970e20e0050b8728cc83ed1b996ce9005d4c36.pem](excluded/c3f697a92a293d86f9a3ee7ccb970e20e0050b8728cc83ed1b996ce9005d4c36.pem)

## Excluded Managed CAs

### DigiCert

  * [7cac9a0ff315387750ba8bafdb1c2bc29b3f0bba16362ca93a90f84da2df5f3e.pem](managed/7cac9a0ff315387750ba8bafdb1c2bc29b3f0bba16362ca93a90f84da2df5f3e.pem)
  * [ac50b5fb738aed6cb781cc35fbfff7786f77109ada7c08867c04a573fd5cf9ee.pem](managed/ac50b5fb738aed6cb781cc35fbfff7786f77109ada7c08867c04a573fd5cf9ee.pem)
