from __future__ import annotations

import logging
import sys
from multiprocessing import Queue
from queue import Empty
from struct import pack
from typing import Any

from PySide6.QtBluetooth import (
    QBluetoothDeviceDiscoveryAgent,
    QBluetoothDeviceInfo,
    QBluetoothUuid,
    QLowEnergyCharacteristic,
    QLowEnergyController,
    QLowEnergyService,
)
from PySide6.QtCore import QCoreApplication, QObject, QTimer, Signal
from PySide6.QtWidgets import QApplication

from .keyboard import KeyboardEvent

log = logging.getLogger(__name__)


class DeviceFinder(QObject):
    device_found_signal: Signal = Signal(QBluetoothDeviceInfo)
    _device_discovery_agent: QBluetoothDeviceDiscoveryAgent
    _device_name: str
    _device: QBluetoothDeviceInfo | None = None

    def __init__(self, *args: Any, device_name: str, **kwargs: Any):
        super().__init__(*args, **kwargs)
        self._device_name = device_name

        self._device_discovery_agent = QBluetoothDeviceDiscoveryAgent(self)
        self._device_discovery_agent.setLowEnergyDiscoveryTimeout(60000)
        self._device_discovery_agent.deviceDiscovered.connect(self.add_device)
        self._device_discovery_agent.errorOccurred.connect(self.scan_error)
        self._device_discovery_agent.start(
            QBluetoothDeviceDiscoveryAgent.DiscoveryMethod.LowEnergyMethod
        )

    def add_device(self, device: QBluetoothDeviceInfo) -> None:
        if device_name := device.name():
            if device_name == self._device_name:
                log.info("Found required device %s", device_name)
                self._device = device
                self._device_discovery_agent.stop()
                self.device_found_signal.emit(device)

    def scan_canceled(self) -> None:
        log.info("Scan canceled")

    def scan_finished(self) -> None:
        if self._device is None:
            log.info(
                'Scan finished, device "%s" not found. Retrying.', self._device_name
            )
            self._device_discovery_agent.start(
                QBluetoothDeviceDiscoveryAgent.DiscoveryMethod.LowEnergyMethod
            )

    def scan_error(self) -> None:
        log.error("Scan error. Exiting.")
        self.quit()

    def quit(self) -> None:
        if instance := QCoreApplication.instance():
            instance.quit()


class DeviceConnector(QObject):
    controller: QLowEnergyController
    service: QLowEnergyService

    @property
    def ble_controller_ready(self) -> bool:
        return hasattr(self, "controller")

    def check_ble_controller(self) -> None:
        if not self.ble_controller_ready:
            raise RuntimeError("BLE controller not initialized")
        return None

    def device_found(self, device: QBluetoothDeviceInfo) -> None:
        self.controller = QLowEnergyController.createCentral(device, self)
        self.controller.errorOccurred.connect(self.error_occurred)
        self.controller.connected.connect(self.connected)
        self.controller.disconnected.connect(self.disconnected)
        self.controller.serviceDiscovered.connect(self.service_discovered)
        self.controller.connectToDevice()

    def error_occurred(self, *args: Any, **kwargs: Any) -> None:
        log.error("BT connection error.")

    def connected(self, *args: Any, **kwargs: Any) -> None:
        log.info("BT connected.")
        self.check_ble_controller()
        self.controller.discoverServices()

    def disconnected(self, *args: Any, **kwargs: Any) -> None:
        log.info("BT disconnected.")
        self.check_ble_controller()
        self.controller.connectToDevice()

    def service_discovered(self, service_uuid: QBluetoothUuid) -> None:
        log.info("GATT service discovered.")
        self.check_ble_controller()
        self.service = self.controller.createServiceObject(service_uuid, self)
        self.service.discoverDetails()


class KeyboardEventSender(QObject):
    _device_connector: DeviceConnector
    _queue: Queue[KeyboardEvent]
    _timer: QTimer
    _timer_ping: QTimer
    _PING_INTERVAL = 1000
    _PING_DATA = b"\x00\x00\x00"

    def __init__(
        self,
        *args: Any,
        device_connector: DeviceConnector,
        queue: Queue[KeyboardEvent],
        interval: int = 10,
        **kwargs: Any,
    ):
        super().__init__(*args, **kwargs)
        log.info("Interval %d", interval)
        self._device_connector = device_connector
        self._queue = queue
        self._timer = QTimer(self, interval=interval)
        self._timer.timeout.connect(self._process_keyboard_event)
        self._timer.start()

        # Not sure whether ping is really necessary
        self._timer_ping = QTimer(self, interval=self._PING_INTERVAL)
        self._timer_ping.timeout.connect(self._ping)
        self._timer_ping.start()

    def _serialize_event(self, pressed: bool, scancode: int, modifiers: int) -> bytes:
        return pack(">BBB", pressed, scancode, modifiers)

    def _get_keyboard_event_data(self) -> bytes | None:
        try:
            pressed, scancode, modifiers = self._queue.get_nowait()
            return self._serialize_event(pressed, scancode, modifiers)
        except Empty:
            return None
        except ValueError as e:
            log.error("Error getting keyboard event", exc_info=e)
            self._timer.stop()
            return None

    def _get_characteristic(self) -> QLowEnergyCharacteristic | None:
        if not self._device_connector.ble_controller_ready:
            return None
        if (
            self._device_connector.controller.state()
            != QLowEnergyController.ControllerState.DiscoveredState
        ):
            return None
        service = self._device_connector.service
        characteristics = service.characteristics()
        if not characteristics:
            log.error("No GATT characteristics available.")
            return None
        return characteristics[0]

    def _send_characteristic(
        self,
        characteristic: QLowEnergyCharacteristic,
        data: bytes,
    ) -> None:
        self._device_connector.service.writeCharacteristic(
            characteristic,
            data,
            QLowEnergyService.WriteMode.WriteWithoutResponse,
        )

    def _process_keyboard_event(self) -> None:
        if (characteristic := self._get_characteristic()) and (
            data := self._get_keyboard_event_data()
        ):
            self._send_characteristic(characteristic, data)

    def _ping(self) -> None:
        if characteristic := self._get_characteristic():
            self._send_characteristic(characteristic, self._PING_DATA)


def run_bt_sender(
    argv: list[str],
    queue: Queue[KeyboardEvent],
    device_name: str,
    interval: int,
    log_level: int,
) -> None:
    logging.basicConfig(level=log_level)

    app = QApplication(argv)

    device_finder = DeviceFinder(app, device_name=device_name)
    device_connector = DeviceConnector(app)
    device_finder.device_found_signal.connect(device_connector.device_found)
    KeyboardEventSender(
        app,
        device_connector=device_connector,
        queue=queue,
        interval=interval,
    )
    sys.exit(app.exec())
