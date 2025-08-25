FROM ubuntu@sha256:79efa276fdefa2ee3911db29b0608f8c0561c347ec3f4d4139980d43b168d991

# Update the package database and install required tools
RUN apt update && apt install -y --no-install-recommends \
	python3 python3-pip python3-requests pipx git less vim \
    sudo cmake make g++-15 libjemalloc-dev clang-format clangd-19 \
    gnuplot net-tools libasio-dev libboost-all-dev libgtest-dev \
    netcat-traditional wget \
	&& apt-get clean \
    && rm -rf /var/lib/apt/lists/*

RUN pip3 install --no-cache-dir --break-system-packages pytest

# Update  gcc/g++ version to the latest ones
RUN ln -fs /usr/bin/g++-15 /usr/bin/g++
RUN ln -fs /usr/bin/gcc-15 /usr/bin/gcc

# Create a non-root user 'dev'
RUN useradd -ms /bin/bash dev \
    && echo "dev ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers.d/dev \
    && chmod 0440 /etc/sudoers.d/dev

# Switch to the 'dev' user
USER dev

RUN git config --global core.editor code

# Set the default command
CMD ["/bin/bash"]
