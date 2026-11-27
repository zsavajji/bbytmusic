# Build image for bbmusicd: bb10-cxx20 (clang 19 + libc++ for QNX 6.6/ARMv7) plus the
# libraries the engine links statically, each pinned by sha256 and installed in /opt/bbmusic.
#
#   docker build -t bbmusic-build docker
#
# Link line for a program: -I/opt/bbmusic/include /opt/bbmusic/lib/libcurl.a libssl.a libcrypto.a libopus.a
#   /opt/bb10-sdk/target/qnx6/armle-v7/usr/lib/libz.a -lsocket
# zlib goes in by its .a on purpose: the phone's libz.so.2 exports a libgcc unwinder, and a
# shared library that does so ahead of the C++ runtime on the link line takes over exception
# handling and crashes on the first throw (docs/plan/phase0.md, gate 0.3).
# 19.1.7-6 compiles for NEON by default (every BB10 CPU has it), so OpenSSL, curl and the engine get it too
# (the user, 2026-11-27, as Tengram's 8eae9da). libopus below asked for it already.
FROM bb10-cxx20:19.1.7-6-build
ENV BBM=/opt/bbmusic TC=/opt/bb10-cxx20/share/bb10-cxx20.cmake BINUTILS=/opt/bb10-toolchain/bin/arm-unknown-nto-qnx6.5.0eabi

# OpenSSL instead of wolfSSL (the user, 2026-11-27), built as Phoenix builds it: wolfSSL marks
# its AES state 16-byte aligned but mallocs it (QNX gives 8), and with NEON on the compiler's
# aligned stores there died of a bus error in the first handshake (1.0.2.80 on the Passport).
# perl: the full distribution (FindBin etc.) for OpenSSL's Configure and perlasm. patch: applies
# the QNX patch below.
RUN apt-get update && apt-get install -y --no-install-recommends perl patch make \
	&& rm -rf /var/lib/apt/lists/*

# OpenSSL: static, with OpenSSL's own ARM assembly (linux-armv4: NEON AES, SHA, bignum). The patch
# is required: without it the NEON probes in OPENSSL_cpuid_setup kill the thread's FPU on BB10.
# No engines/dso/async. -DNO_SYSLOG: the SDK has no <syslog.h>. Only the two libraries are built
# (`make build_libs` also builds apps/, which needs syslog.h); build_generated first, or the SDK's
# old OpenSSL headers get picked up instead of the generated ones.
ADD --checksum=sha256:a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8 \
	https://www.openssl.org/source/openssl-3.5.7.tar.gz /tmp/openssl.tar.gz
COPY patches/openssl-0001-qnx-armcap.patch /tmp/openssl-0001-qnx-armcap.patch
RUN cd /tmp && tar xzf openssl.tar.gz && cd openssl-3.5.7 \
	&& patch -p1 < /tmp/openssl-0001-qnx-armcap.patch \
	&& CC=bb10-clang AR=$BINUTILS-ar RANLIB=$BINUTILS-ranlib \
		./Configure linux-armv4 no-shared no-tests no-dso no-engine no-async -DNO_SYSLOG -O2 \
			-ffunction-sections -fdata-sections --prefix=$BBM --openssldir=/etc/ssl >/dev/null \
	&& make -s -j"$(nproc)" build_generated >/dev/null \
	&& make -s -j"$(nproc)" libcrypto.a libssl.a >/dev/null \
	&& mkdir -p $BBM/lib $BBM/include && cp libcrypto.a libssl.a $BBM/lib/ \
	&& cp -RL include/openssl $BBM/include/ \
	&& $BINUTILS-nm $BBM/lib/libcrypto.a | grep -q sha256_block_data_order_neon \
	&& cd / && rm -rf /tmp/openssl*

# libcurl: HTTP and HTTPS only, OpenSSL backend (the one above, by full path: never the SDK's old
# libssl), the SDK's zlib for gzip. No CA path is compiled in: the engine hands curl its own roots.
ADD --checksum=sha256:e3de77cfa6367083c776a26647fc9f633b538c06c3baa73d931a252ac8f72b49 \
	https://github.com/curl/curl/releases/download/curl-8_23_0/curl-8.23.0.tar.gz /tmp/curl.tar.gz
RUN cd /tmp && tar xzf curl.tar.gz \
	&& off="DICT FILE FTP GOPHER IMAP LDAP LDAPS MQTT POP3 RTSP SMTP TELNET TFTP IPFS WEBSOCKETS \
		NTLM ALTSVC HSTS NETRC AWS KERBEROS_AUTH NEGOTIATE_AUTH DIGEST_AUTH FORM_API MIME DOH" \
	&& flags="" && for p in $off; do flags="$flags -DCURL_DISABLE_$p=ON"; done \
	&& cmake -S curl-8.23.0 -B curl-build -DCMAKE_TOOLCHAIN_FILE=$TC -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=$BBM -DOPENSSL_ROOT_DIR=$BBM -DOPENSSL_USE_STATIC_LIBS=ON \
		-DOPENSSL_INCLUDE_DIR=$BBM/include -DOPENSSL_SSL_LIBRARY=$BBM/lib/libssl.a \
		-DOPENSSL_CRYPTO_LIBRARY=$BBM/lib/libcrypto.a \
		-DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_CURL_EXE=OFF -DBUILD_LIBCURL_DOCS=OFF \
		-DBUILD_MISC_DOCS=OFF -DBUILD_TESTING=OFF -DCURL_USE_OPENSSL=ON -DCURL_USE_WOLFSSL=OFF \
		-DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF -DUSE_NGHTTP2=OFF -DUSE_LIBIDN2=OFF -DCURL_BROTLI=OFF \
		-DCURL_ZSTD=OFF -DCURL_ZLIB=ON -DENABLE_UNIX_SOCKETS=OFF -DCURL_CA_BUNDLE=none -DCURL_CA_PATH=none \
		$flags >/tmp/curl-configure.log \
	&& grep -E "OpenSSL|OPENSSL" /tmp/curl-configure.log \
	&& grep -q "^OPENSSL_SSL_LIBRARY:.*=$BBM/lib/libssl.a" curl-build/CMakeCache.txt \
	&& grep -q "^OPENSSL_CRYPTO_LIBRARY:.*=$BBM/lib/libcrypto.a" curl-build/CMakeCache.txt \
	&& cmake --build curl-build -j"$(nproc)" >/dev/null && cmake --install curl-build >/dev/null \
	&& grep -E "^Libs|^Requires|ssl_backends|SSL" $BBM/lib/pkgconfig/libcurl.pc \
	&& cd / && rm -rf /tmp/curl*

# libopus, as HyperBerry builds it: QNX has no runtime CPU detection, so NEON is presumed
# (every BB10 CPU has it) and fixed at configure time.
ADD --checksum=sha256:6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1 \
	https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz /tmp/opus.tar.gz
RUN cd /tmp && tar xzf opus.tar.gz && cd opus-1.6.1 \
	&& ./configure --host=arm-unknown-nto-qnx6.5.0eabi CC=bb10-clang CFLAGS="-O2 -mfpu=neon" --disable-rtcd \
		--enable-static --disable-shared --disable-doc --disable-extra-programs --prefix=$BBM >/dev/null \
	&& make -j"$(nproc)" >/dev/null && make install >/dev/null \
	&& cd / && rm -rf /tmp/opus*
