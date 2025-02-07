from __future__ import annotations

import logging
import os
from collections.abc import Callable
from multiprocessing import Queue
from types import MappingProxyType

import sdl2
import sdl2.ext
from sdl2 import SDL_FreeSurface, timer
from sdl2.ext import Color, FontTTF, Texture
from sdl2.sdlttf import TTF_Init

log = logging.getLogger(__name__)

FONT_PATH = os.path.join(
    os.path.dirname(__file__),
    "assets",
    "arial.ttf",
).encode()
FONT_SIZE = 36

WINDOW_WIDTH = 360
WINDOW_HEIGHT = 120

SDL_DELAY = 50
BG_COLOR = Color(0, 0, 0)

SDL_TO_HID_MODIFIER = MappingProxyType(
    {
        sdl2.KMOD_LSHIFT: 0x02,
        sdl2.KMOD_RSHIFT: 0x20,
        sdl2.KMOD_LCTRL: 0x01,
        sdl2.KMOD_RCTRL: 0x10,
        sdl2.KMOD_LALT: 0x04,
        sdl2.KMOD_RALT: 0x40,
        sdl2.KMOD_LGUI: 0x08,
        sdl2.KMOD_RGUI: 0x80,
    }
)

MODIFIERS = MappingProxyType(
    {
        getattr(sdl2.keycode, attr): attr
        for attr in dir(sdl2.keycode)
        if (
            attr.startswith("KMOD_")
            and attr
            not in (
                "KMOD_CTRL",
                "KMOD_SHIFT",
                "KMOD_ALT",
                "KMOD_GUI",
            )
        )
    }
)

SCANCODES = MappingProxyType(
    {
        getattr(sdl2.scancode, attr): attr
        for attr in dir(sdl2.scancode)
        if (
            attr.startswith("SDL_SCANCODE_")
            and attr
            not in (
                "SDL_SCANCODE_LCTRL",
                "SDL_SCANCODE_LSHIFT",
                "SDL_SCANCODE_LALT",
                "SDL_SCANCODE_LGUI",
                "SDL_SCANCODE_RCTRL",
                "SDL_SCANCODE_RSHIFT",
                "SDL_SCANCODE_RALT",
                "SDL_SCANCODE_RGUI",
            )
        )
    }
)

KeyboardEvent = tuple[bool, int, int]


class KeyboardCapturer:
    _font: FontTTF
    _window: sdl2.ext.Window
    _renderer: sdl2.ext.Renderer
    _queue: Queue[KeyboardEvent]
    _sender_checker: Callable[[], bool]
    _sdl_delay: int

    def __init__(
        self,
        queue: Queue[KeyboardEvent],
        sender_checker: Callable[[], bool],
        sdl_delay: int,
    ):
        sdl2.ext.init()
        TTF_Init()
        self._window = sdl2.ext.Window(
            "USB Keyboard BT Proxy",
            size=(WINDOW_WIDTH, WINDOW_HEIGHT),
            position=(sdl2.SDL_WINDOWPOS_CENTERED, sdl2.SDL_WINDOWPOS_CENTERED),
            flags=sdl2.SDL_WINDOW_RESIZABLE,
        )

        render_flags = sdl2.SDL_RENDERER_SOFTWARE
        self._renderer = sdl2.ext.Renderer(self._window, flags=render_flags)

        text_color = Color()
        self._font = FontTTF(FONT_PATH, FONT_SIZE, color=text_color)

        self._sender_checker = sender_checker
        self._queue = queue
        self._sdl_delay = sdl_delay

    def _sdl_modifier_to_hid(self, modifiers: int) -> int:
        hid_modifiers = 0
        for sdl_mod, hid_mod in SDL_TO_HID_MODIFIER.items():
            if modifiers & sdl_mod:
                hid_modifiers |= hid_mod
        return hid_modifiers

    def _enqueue_keyboard_event(self, pressed: bool, event: sdl2.SDL_Event) -> None:
        scancode = event.key.keysym.scancode
        modifiers = self._sdl_modifier_to_hid(event.key.keysym.mod)
        self._queue.put((pressed, scancode, modifiers))

    @staticmethod
    def _get_modifier_names(modifier: int) -> list[str]:
        names = []
        for code, name in MODIFIERS.items():
            if code & modifier:
                names.append(name.removeprefix("KMOD_"))
        return names

    @staticmethod
    def _get_scancode_name(code: int) -> str | None:
        name = SCANCODES.get(code)
        if not name:
            return None
        return name.removeprefix("SDL_SCANCODE_")

    def _render_keyboard_event(self, event: sdl2.SDL_Event) -> None:
        scancode = event.key.keysym.scancode
        modifier = event.key.keysym.mod

        parts: list[str] = []
        if modifier != sdl2.keycode.KMOD_NONE:
            parts.extend(self._get_modifier_names(modifier))

        if event.type == sdl2.SDL_KEYDOWN and (
            scancode_name := self._get_scancode_name(scancode)
        ):
            parts.append(scancode_name)

        self._renderer.clear(BG_COLOR)
        if not parts:
            self._renderer.present()
            return

        text = " + ".join(parts)
        surface = self._font.render_text(text, width=WINDOW_WIDTH)
        texture = Texture(self._renderer, surface)
        SDL_FreeSurface(surface)
        self._renderer.copy(texture)
        self._renderer.present()
        texture.destroy()

    def run(self) -> None:
        self._window.show()
        self._renderer.clear(BG_COLOR)
        self._renderer.present()

        running = True
        while running:
            events = sdl2.ext.get_events()
            for event in events:
                if event.type == sdl2.SDL_QUIT:
                    running = False
                    break

                if event.type in (
                    sdl2.SDL_KEYDOWN,
                    sdl2.SDL_KEYUP,
                ):
                    pressed = event.type == sdl2.SDL_KEYDOWN
                    self._enqueue_keyboard_event(pressed=pressed, event=event)
                    self._render_keyboard_event(event=event)

            self._window.refresh()
            timer.SDL_Delay(self._sdl_delay)

            if not self._sender_checker():
                log.info("Sender is not alive. Exiting.")
                running = False
