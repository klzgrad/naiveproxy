#!/bin/sh
set -e

name='example'

echo "
[req]
prompt = no
distinguished_name = dn
[dn]
CN = $name
[san]
subjectAltName = DNS:$name" >site.cnf

openssl genrsa -out ca.key 2048
openssl req -x509 -new -nodes -key ca.key -days 365 -out ca.pem -subj '/CN=Test Root CA'

openssl genrsa -out $name.key.rsa 2048
openssl ecparam -genkey -name prime256v1 -out $name.key.ecdsa
for key in rsa ecdsa; do
  openssl req -new -nodes -key $name.key.$key -out $name.csr.$key -reqexts san -config site.cnf
  openssl x509 -req -in $name.csr.$key -CA ca.pem -CAkey ca.key -CAcreateserial -out $name.pem.$key -days 365 -extensions san -extfile site.cnf
  cat $name.key.$key >>$name.pem.$key
  rm $name.key.$key $name.csr.$key
done

rm ca.key ca.srl site.cnf

echo
echo 'To trust the test CA:'
echo '  certutil -d "sql:$HOME/.pki/nssdb" -A -t C,, -n 'Test Root CA' -i ca.pem'
echo
