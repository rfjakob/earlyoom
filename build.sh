#!/usr/bin/env bash
set -euo pipefail

cd $(dirname ${BASH_SOURCE[0]})
WORK_DIR=$(pwd)

# install git pandoc
if [ -d /etc/yum.repos.d ]; then
    echo "REHL-based"
    sudo yum install -y git pandoc
elif [ -d /etc/apt ]; then
    echo "Debian-based"
    sudo apt install -y  git pandoc
else
    echo "Unknown distribution"
    exit 1
fi

if [ -d /etc/yum.repos.d ]; then
    # remove parameter '-k' which gzip not supports in CentOS
    sed -i 's/-f -k -n/-f -n/' Makefile
    cat Makefile  | grep gzip
fi

# info
tag=v1.7.x && version=$tag
echo "version: $version"
arch=$(uname -m)
echo "arch: $arch"

cd $WORK_DIR
git show-ref --tags --quiet --verify -- "refs/tags/$tag"
if [ $? -ne 0 ]; then
    echo -e "Tag \033[31m $tag \033[0m not exist. exit..."
    exit 1
fi

origin_branch=$(git rev-parse --abbrev-ref HEAD)
# create a new branch by tag
git checkout -b package$$ $tag

make clean
# static build
make CFLAGS='-static'
make earlyoom.1.gz
# verify the static binary
set +e
ls -alh earlyoom && ldd earlyoom
set -e

# package
package_dir=earlyoom-$version
rm -rf $package_dir && mkdir -pv $package_dir

cp earlyoom.service earlyoom earlyoom.default earlyoom.1.gz $package_dir/

cd $package_dir
file_release_info=current_revision
> $file_release_info
echo "Write release version info to $file_release_info"
echo "version: "$version >> $file_release_info
echo "pub time: "$(date '+%Y-%m-%d %H:%M:%S') >> $file_release_info
echo "arch: $arch" >> $file_release_info
echo "commit id: $(git rev-parse HEAD)" >> $file_release_info

cd $WORK_DIR
pkg=earlyoom-${arch}-${version}.tar.gz && rm -rf $pkg

tar -czvf $pkg $package_dir
rm -rf $package_dir

# To clean up local branchs for package
git checkout $origin_branch
git fetch --prune && git branch -vv | grep -v origin | grep package | awk '{print $1}' | xargs git branch -D
# package info
ls -alh $WORK_DIR/$pkg
md5sum $pkg
