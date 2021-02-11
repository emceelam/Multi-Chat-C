FROM alpine:latest AS builder

COPY makefile /root
COPY server.c /root

WORKDIR /root
RUN apk add g++ make libc6-compat
RUN make server


FROM alpine:latest

LABEL maintainer="Lambert Lum"
LABEL description="Multi-client Chat Server"

COPY --from=builder /root/server /root
ENTRYPOINT ["/root/server"]

EXPOSE 4020/tcp

