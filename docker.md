# Running Docker

sudo usermod -aG docker emceelam

docker image build --tag multi_chat_c:latest -f Dockerfile .

docker container stop multi_chat_c
  # stop the previous multi_chat_c docker

docker container run \
  --rm \
  --detach \
  --name multi_chat_c \
  --publish 127.0.0.1:4020:4020 \
  multi_chat_c:latest

docker container ls

# from one terminal
telnet 127.0.0.1 4020

# from another terminal
telnet 127.0.0.1 4020

# from yet another terminal
telnet 127.0.0.1 4020

# if you want to look inside
docker exec --interactive --tty multi_chat_c /bin/sh


# second container
docker container run \
  --rm \
  --detach \
  --name multi_chat_c-4021 \
  --publish 127.0.0.1:4021:4020 \
  multi_chat_c:latest


# from one terminal
telnet 127.0.0.1 4021

# from another terminal
telnet 127.0.0.1 4021
