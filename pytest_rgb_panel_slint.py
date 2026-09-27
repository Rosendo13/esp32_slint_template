# SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: CC0-1.0

import pytest
from pytest_embedded import Dut


@pytest.mark.esp32s3
@pytest.mark.octal_psram
@pytest.mark.parametrize(
    'config',
    [
        'single_fb_with_bb',
        'single_fb_no_bb',
        'double_fb',
    ],
    indirect=True,
)
def test_rgb_lcd_slint(dut: Dut) -> None:
    # display.cpp
    dut.expect_exact('display: I2C master bus ready')
    dut.expect(r'display: Install RGB LCD panel \(\d framebuffers? in PSRAM\)')
    dut.expect_exact('display: GT911 touch controller ready')

    # ui_hal.cpp -- which rendering mode Slint ended up in depends on
    # CONFIG_EXAMPLE_DOUBLE_FB, so match either.
    dut.expect(r'ui_hal: Slint platform: (double-buffered|line-by-line), 800x480')

    # bsp.cpp / main.cpp -- QP is up and the event loop has taken over
    dut.expect_exact('bsp: QF started.')
    dut.expect_exact('main: Active Objects started; entering Slint event loop')
