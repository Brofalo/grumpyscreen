#!/bin/bash

CURRENT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd -P)"

PRINTER_IP=
SETUP=false
TARGET=mips

GIT_REVISION=$(git rev-parse HEAD)
GIT_BRANCH=$(git rev-parse --abbrev-ref HEAD)

function docker_make() {
    local target_env=()
    if [ "$GUPPY_SMALL_SCREEN" = "true" ]; then
        target_env=(GUPPY_SMALL_SCREEN=true GUPPY_CALIBRATE=true)
    elif [ "$TARGET" = "rpi" ]; then
        target_env=(GUPPY_CALIBRATE=true)
    fi

    echo "Target Arguments: ${target_env[*]}"
    # Every value crosses into the container as its own argv element, and the
    # script the inner bash runs contains no interpolation at all. The make
    # targets used to be pasted into that script as `make $@`, so the shell
    # inside docker re-parsed them and a build argument carrying shell
    # metacharacters ran as a command. `env` takes the KEY=VALUE pairs and then
    # the command, which keeps the deliberate splitting of the target
    # assignments without exposing any of them to a shell.
    docker run -ti -v "$PWD:$PWD" pellcorp/guppydev /bin/bash -c \
        'cd "$1" || exit 1; shift; exec env "$@"' \
        _ "$PWD" \
        "GUPPYSCREEN_VERSION=${GIT_REVISION}" \
        "GUPPYSCREEN_BRANCH=${GIT_BRANCH}" \
        "CROSS_COMPILE=${CROSS_COMPILE}" \
        "${target_env[@]}" \
        make "$@"
}

TARGET=
GUPPY_SMALL_SCREEN=false
SETUP=false
PI_USERNAME=pi

while true; do
    if [ "$1" = "--setup" ]; then
        shift
        SETUP=true
        TARGET=$1
        if [ "$TARGET" != "mips" ] && [ "$TARGET" != "rpi" ]; then
          echo "ERROR: mips or rpi target must be specified"
          exit 1
        fi
        shift
    elif [ "$1" = "--small" ]; then
        export GUPPY_SMALL_SCREEN=true
        shift
    elif [ "$1" = "--username" ] && [ -n "$2" ]; then
        export PI_USERNAME=$2
        shift
        shift
    elif [ "$1" = "--printer" ]; then
        shift
        PRINTER_IP=$1
        shift
    else
        break
    fi
done

if [ "$SETUP" = "true" ]; then
  if [ "$TARGET" = "rpi" ]; then
    echo "rpi" > $CURRENT_DIR/.target.cfg
    echo "username=$PI_USERNAME" >> $CURRENT_DIR/.target.cfg
  else
    echo "mips" > $CURRENT_DIR/.target.cfg
  fi

  if [ "$GUPPY_SMALL_SCREEN" = "true" ]; then
    echo "small=true" >> $CURRENT_DIR/.target.cfg
  fi
fi

if [ -f $CURRENT_DIR/.target.cfg ]; then
  TARGET=$(cat $CURRENT_DIR/.target.cfg | head -1)
  if [ $(cat $CURRENT_DIR/.target.cfg | grep "small=true" | wc -l) -gt 0 ]; then
    export GUPPY_SMALL_SCREEN=true
  fi
  if [ $(cat $CURRENT_DIR/.target.cfg | grep "username=" | wc -l) -gt 0 ]; then
    export PI_USERNAME=$(cat $CURRENT_DIR/.target.cfg | grep "username=" | awk -F '=' '{print $2}')
  fi
fi

if [ "$TARGET" = "rpi" ]; then
  export CROSS_COMPILE=armv8-rpi3-linux-gnueabihf-
else
  export CROSS_COMPILE=mipsel-buildroot-linux-musl-
fi

if [ "$SETUP" = "true" ]; then
    docker_make libhvclean || exit $?
    docker_make wpaclean || exit $?
    docker_make clean || exit $?

    docker_make libhv.a || exit $?
    docker_make wpaclient || exit $?
else
    docker_make "$1" || exit $?

    if [ -n "$PRINTER_IP" ] && [ -f build/bin/guppyscreen ]; then
        case "$PRINTER_IP" in
          ""|*[!A-Za-z0-9.\-]*|-*)
            echo "ERROR: refusing PRINTER_IP with unexpected characters: $PRINTER_IP" >&2
            exit 1
            ;;
        esac
        if [ "$TARGET" = "mips" ]; then
          # The credential comes from the environment, never from source. This
          # deploy path is inherited from upstream and targets a Creality K1 over
          # scp; the Centauri Carbon this fork ships on updates over signed SWU
          # and never takes this route, so it is a developer convenience only.
          # sshpass -e reads SSHPASS from the environment instead of argv, so the
          # value is not visible to `ps` for every other user on the box.
          if [ -z "${K1_ROOT_PW:-}" ]; then
            echo "ERROR: set K1_ROOT_PW to deploy to a mips (Creality K1) target" >&2
            exit 1
          fi
          export SSHPASS="$K1_ROOT_PW"
          sshpass -e scp build/bin/guppyscreen root@"$PRINTER_IP":
          sshpass -e ssh root@"$PRINTER_IP" "mv /root/guppyscreen /usr/data/guppyscreen/"

          cp grumpyscreen.cfg /tmp
          if [ "$GUPPY_SMALL_SCREEN" = "true" ]; then
            sed -i 's/display_rotate: 3/display_rotate: 1/g' /tmp/grumpyscreen.cfg
          fi
          sshpass -e scp /tmp/grumpyscreen.cfg root@"$PRINTER_IP":
          sshpass -e ssh root@"$PRINTER_IP" "mv /root/grumpyscreen.cfg /usr/data/printer_data/config/grumpyscreen.ini"
          sshpass -e ssh root@"$PRINTER_IP" "/etc/init.d/S99guppyscreen restart"
        else # rpi
          case "$PI_USERNAME" in
            ""|*[!A-Za-z0-9._\-]*|-*)
              echo "ERROR: refusing PI_USERNAME with unexpected characters: $PI_USERNAME" >&2
              exit 1
              ;;
          esac
          echo "Uploading to ${PI_USERNAME}@$PRINTER_IP ..."
          cp grumpyscreen.cfg /tmp
          scp build/bin/guppyscreen "$PI_USERNAME"@"$PRINTER_IP":/tmp/
          sed -i 's/display_rotate: 3/display_rotate: 0/g' /tmp/grumpyscreen.cfg
          sed -i '/S58factoryreset/d' /tmp/grumpyscreen.cfg
          # rpi does not have switch to stock
          sed -i 's:/usr/data/pellcorp/k1/switch-to-stock.sh::g' /tmp/grumpyscreen.cfg
          # for now no support command for rpi either
          sed -i 's:/usr/data/pellcorp/tools/support.sh::g' /tmp/grumpyscreen.cfg
          scp /tmp/grumpyscreen.cfg "$PI_USERNAME"@"$PRINTER_IP":/tmp/
          ssh "$PI_USERNAME"@"$PRINTER_IP" "mv /tmp/guppyscreen /home/$PI_USERNAME/guppyscreen/"
          ssh "$PI_USERNAME"@"$PRINTER_IP" "mv /tmp/grumpyscreen.cfg /home/$PI_USERNAME/printer_data/config/grumpyscreen.ini"
          ssh "$PI_USERNAME"@"$PRINTER_IP" "sudo systemctl restart grumpyscreen"
        fi
    fi
fi
