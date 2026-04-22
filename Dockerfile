FROM ubuntu:latest

RUN apt-get update && apt-get install -y \
    gcc \
    make \
    valgrind \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

CMD ["bash"]
