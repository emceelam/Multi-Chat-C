FROM alpine:latest

LABEL maintainer="Lambert Lum"
LABEL description="Multi-client Chat Server"

COPY makefile /root
COPY server.c /root

WORKDIR /root
RUN apk upgrade \
  && apk update \
  && apk add g++ make libc6-compat \
  && make server \
  && apk del g++ make 

ENTRYPOINT ["./server"]

EXPOSE 4020/tcp

