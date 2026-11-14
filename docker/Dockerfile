# Build image for bbmusicd: bb10-cxx20 (clang 19 + libc++ for QNX 6.6/ARMv7) plus the three
# libraries the engine links statically, each pinned by sha256 and installed in /opt/bbmusic.
#
#   docker build -t bbmusic-build docker
#
# Link line for a program: -I/opt/bbmusic/include -L/opt/bbmusic/lib -lcurl -lwolfssl -lopus
#   /opt/bb10-sdk/target/qnx6/armle-v7/usr/lib/libz.a -lsocket
# zlib goes in by its .a on purpose: the phone's libz.so.2 exports a libgcc unwinder, and a
# shared library that does so ahead of the C++ runtime on the link line takes over exception
# handling and crashes on the first throw (docs/plan/phase0.md, gate 0.3).
FROM bb10-cxx20:latest
ENV BBM=/opt/bbmusic TC=/opt/bb10-cxx20/share/bb10-cxx20.cmake

# wolfSSL: TLS 1.3 and 1.2 for libcurl. The tag's source archive (the release has no tarball).
ADD --checksum=sha256:7256bfc89b183a75183806c7debfa203443873b0b4a562e1b80d68e01b45ac57 \
	https://github.com/wolfSSL/wolfssl/archive/refs/tags/v5.9.4-stable.tar.gz /tmp/wolfssl.tar.gz
RUN cd /tmp && tar xzf wolfssl.tar.gz \
	&& cmake -S wolfssl-5.9.4-stable -B wolfssl-build -DCMAKE_TOOLCHAIN_FILE=$TC -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=$BBM -DBUILD_SHARED_LIBS=OFF -DWOLFSSL_CURL=yes -DWOLFSSL_TLS13=yes \
		-DWOLFSSL_EXAMPLES=no -DWOLFSSL_CRYPT_TESTS=no >/dev/null \
	&& cmake --build wolfssl-build -j"$(nproc)" >/dev/null && cmake --install wolfssl-build >/dev/null \
	&& rm -rf /tmp/wolfssl*

# libcurl: HTTP and HTTPS only, wolfSSL backend, the SDK's zlib for gzip. No CA path is
# compiled in: the engine hands curl its own root bundle.
ADD --checksum=sha256:e3de77cfa6367083c776a26647fc9f633b538c06c3baa73d931a252ac8f72b49 \
	https://github.com/curl/curl/releases/download/curl-8_23_0/curl-8.23.0.tar.gz /tmp/curl.tar.gz
RUN cd /tmp && tar xzf curl.tar.gz \
	&& off="DICT FILE FTP GOPHER IMAP LDAP LDAPS MQTT POP3 RTSP SMTP TELNET TFTP IPFS WEBSOCKETS \
		NTLM ALTSVC HSTS NETRC AWS KERBEROS_AUTH NEGOTIATE_AUTH DIGEST_AUTH FORM_API MIME DOH" \
	&& flags="" && for p in $off; do flags="$flags -DCURL_DISABLE_$p=ON"; done \
	&& cmake -S curl-8.23.0 -B curl-build -DCMAKE_TOOLCHAIN_FILE=$TC -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=$BBM -DWOLFSSL_INCLUDE_DIR=$BBM/include -DWOLFSSL_LIBRARY=$BBM/lib/libwolfssl.a \
		-DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_CURL_EXE=OFF -DBUILD_LIBCURL_DOCS=OFF \
		-DBUILD_MISC_DOCS=OFF -DBUILD_TESTING=OFF -DCURL_USE_WOLFSSL=ON -DCURL_USE_OPENSSL=OFF \
		-DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF -DUSE_NGHTTP2=OFF -DUSE_LIBIDN2=OFF -DCURL_BROTLI=OFF \
		-DCURL_ZSTD=OFF -DCURL_ZLIB=ON -DENABLE_UNIX_SOCKETS=OFF -DCURL_CA_BUNDLE=none -DCURL_CA_PATH=none \
		$flags >/dev/null \
	&& cmake --build curl-build -j"$(nproc)" >/dev/null && cmake --install curl-build >/dev/null \
	&& rm -rf /tmp/curl*

# libopus, as HyperBerry builds it: QNX has no runtime CPU detection, so NEON is presumed
# (every BB10 CPU has it) and fixed at configure time.
ADD --checksum=sha256:6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1 \
	https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz /tmp/opus.tar.gz
RUN cd /tmp && tar xzf opus.tar.gz && cd opus-1.6.1 \
	&& ./configure --host=arm-unknown-nto-qnx6.5.0eabi CC=bb10-clang CFLAGS="-O2 -mfpu=neon" --disable-rtcd \
		--enable-static --disable-shared --disable-doc --disable-extra-programs --prefix=$BBM >/dev/null \
	&& make -j"$(nproc)" >/dev/null && make install >/dev/null \
	&& cd / && rm -rf /tmp/opus*
