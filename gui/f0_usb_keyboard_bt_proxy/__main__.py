import enum
import logging
import sys
from ctypes import c_int
from multiprocessing import Process, Queue, Value
from multiprocessing.sharedctypes import Synchronized

import argclass

from .bt import run_bt_sender
from .keyboard import KeyboardCapturer, KeyboardEvent


class LogLevelEnum(enum.IntEnum):
    debug = logging.DEBUG
    info = logging.INFO
    warning = logging.WARNING
    error = logging.ERROR
    critical = logging.CRITICAL


class Parser(argclass.Parser):
    bt_device_name: str
    log_level: LogLevelEnum = argclass.EnumArgument(LogLevelEnum, default="info")
    sender_interval: int = 10
    sdl_delay: int = 50


def main() -> None:
    parser = Parser()
    parser.parse_args()

    logging.basicConfig(level=parser.log_level)
    queue: Queue[KeyboardEvent] = Queue()
    bt_status: Synchronized[int] = Value(c_int)
    sender_process = Process(
        target=run_bt_sender,
        args=(sys.argv,),
        kwargs=dict(
            queue=queue,
            bt_status=bt_status,
            device_name=parser.bt_device_name,
            interval=parser.sender_interval,
            log_level=int(parser.log_level),
        ),
    )
    sender_process.start()
    capturer = KeyboardCapturer(
        queue=queue,
        bt_status=bt_status,
        sender_checker=lambda: sender_process.is_alive(),
        sdl_delay=parser.sdl_delay,
    )
    try:
        capturer.run()
    except KeyboardInterrupt:
        pass
    finally:
        sender_process.kill()
        sender_process.join()


if __name__ == "__main__":
    main()
