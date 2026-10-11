package com.obsphonecamera.app.protocol

import com.obsphonecamera.app.util.Hex
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class PairingUriTest {

    // Same secret as tests/test_pairing.cpp (hex32(7): bytes 07..26).
    private val secretHex = "0708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20212223242526"

    @Test
    fun parsesValidUri() {
        val info = PairingUri.parse(
            "ocp1://pair?h=192.168.1.42&p=47100&s=$secretHex&n=Studio%20PC",
        )!!
        assertEquals("192.168.1.42", info.host)
        assertEquals(47100, info.port)
        assertEquals(secretHex, info.secretHex)
        assertEquals("Studio PC", info.name)
    }

    @Test
    fun secretIsLowercased() {
        val info = PairingUri.parse(
            "ocp1://pair?h=10.0.0.5&p=47100&s=${secretHex.uppercase()}&n=PC",
        )!!
        assertEquals(secretHex, info.secretHex)
    }

    @Test
    fun trimsWhitespaceAndIgnoresUnknownParams() {
        val info = PairingUri.parse(
            "  ocp1://pair?h=10.0.0.5&p=47100&s=$secretHex&n=PC&x=1  ",
        )!!
        assertEquals("10.0.0.5", info.host)
    }

    @Test
    fun missingNameDefaultsToEmpty() {
        val info = PairingUri.parse("ocp1://pair?h=10.0.0.5&p=47100&s=$secretHex")!!
        assertEquals("", info.name)
    }

    @Test
    fun rejectsWrongSchemeOrAuthority() {
        assertNull(PairingUri.parse("http://example.com"))
        assertNull(PairingUri.parse("ocp1://pairx?h=1.2.3.4&p=1&s=$secretHex"))
        assertNull(PairingUri.parse("ocp1://pair"))
        assertNull(PairingUri.parse("garbage"))
        assertNull(PairingUri.parse(""))
    }

    @Test
    fun rejectsMissingOrInvalidFields() {
        assertNull(PairingUri.parse("ocp1://pair?h=1.2.3.4"))
        assertNull(PairingUri.parse("ocp1://pair?h=&p=0&s=&n="))
        assertNull(PairingUri.parse("ocp1://pair?p=47100&s=$secretHex&n=PC"))
        assertNull(PairingUri.parse("ocp1://pair?h=1.2.3.4&p=0&s=$secretHex"))
        assertNull(PairingUri.parse("ocp1://pair?h=1.2.3.4&p=65536&s=$secretHex"))
        assertNull(PairingUri.parse("ocp1://pair?h=1.2.3.4&p=abc&s=$secretHex"))
        assertNull(PairingUri.parse("ocp1://pair?h=1.2.3.4&p=47100&s=nothex"))
        assertNull(PairingUri.parse("ocp1://pair?h=1.2.3.4&p=47100&s=${"ab".repeat(31)}"))
        assertNull(PairingUri.parse("ocp1://pair?h=1.2.3.4&p=47100&s=${"ab".repeat(33)}"))
    }

    @Test
    fun secretHexHelperAgreesWithValidator() {
        assertEquals(true, Hex.isValidHex(secretHex, 64))
        assertEquals(false, Hex.isValidHex(secretHex.drop(1), 64))
    }
}
