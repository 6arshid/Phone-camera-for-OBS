package com.obsphonecamera.app.protocol

import com.obsphonecamera.app.util.Hex
import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

class KeyScheduleTest {

    // Same inputs as tests/test_pairing.cpp (master = hex32(42), nonces 01..10 / f0..e1).
    private val master = Hex.decode("2a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40414243444546474849")
    private val clientNonce = Hex.decode("0102030405060708090a0b0c0d0e0f10")
    private val serverNonce = Hex.decode("f0efeeedecebeae9e8e7e6e5e4e3e2e1")
    private val hello = """{"type":"hello"}"""
    private val challenge = """{"type":"challenge"}"""

    // Golden values computed with an independent JDK (javax.crypto) implementation
    // of PROTOCOL.md §3.2.
    private val expectedKAuth =
        "22ea0613ea2287bd610eb6241833e7285d38094172f6aaf9cec2f62ce8cf11b7"
    private val expectedKCtrl =
        "db7722d694fc863ebc0ed78b622d52367d140e1600d734f414f06ac7e95b9ce8" +
            "a835d08b873a3407edb10a30428ca029de7d1138febf5b6bc485105832d8eb96"
    private val expectedKMedia =
        "d9ed8a36cec3c85d035bb12396f0934586bbb9c9b123fe1c723677bc16911a6ff" +
            "69532b4a1716b30203097f9e709ab774736bf9c7218e109a1b87499f1b6b785"

    @Test
    fun constantsMatchSpec() {
        assertEquals("hkdf-sha256", KeySchedule.KDF_HKDF)
        assertEquals("pbkdf2-sha256", KeySchedule.KDF_PBKDF2)
        assertEquals(100000, KeySchedule.PBKDF2_MIN_ITER)
    }

    @Test
    fun goldenKeySchedule() {
        val keys = KeySchedule.keys(master, clientNonce, serverNonce)
        assertEquals(expectedKAuth, Hex.encode(keys.kAuth))
        assertEquals(expectedKCtrl, Hex.encode(keys.kCtrlEnc + keys.kCtrlMac))
        assertEquals(expectedKMedia, Hex.encode(keys.kMediaEnc + keys.kMediaMac))
        assertEquals(32, keys.kAuth.size)
        assertEquals(32, keys.kCtrlEnc.size)
        assertEquals(32, keys.kCtrlMac.size)
        assertEquals(32, keys.kMediaEnc.size)
        assertEquals(32, keys.kMediaMac.size)
    }

    @Test
    fun keysAreDeterministicAndNonceOrderMatters() {
        val a = KeySchedule.keys(master, clientNonce, serverNonce)
        val b = KeySchedule.keys(master, clientNonce, serverNonce)
        assertEquals(Hex.encode(a.kAuth), Hex.encode(b.kAuth))
        assertEquals(Hex.encode(a.kCtrlEnc), Hex.encode(b.kCtrlEnc))
        assertEquals(Hex.encode(a.kMediaMac), Hex.encode(b.kMediaMac))

        val swapped = KeySchedule.keys(master, serverNonce, clientNonce)
        assertNotEquals(Hex.encode(a.kAuth), Hex.encode(swapped.kAuth))
    }

    @Test
    fun sessionKeyFactories() {
        val keys = KeySchedule.keys(master, clientNonce, serverNonce)
        assertContentEquals(keys.kMediaEnc, keys.mediaKeys().encKey)
        assertContentEquals(keys.kMediaMac, keys.mediaKeys().macKey)
    }

    @Test
    fun transcriptIsSha256OfConcatenatedPayloads() {
        val expected = MessageDigest.getInstance("SHA-256")
            .digest(hello.toByteArray(Charsets.UTF_8) + challenge.toByteArray(Charsets.UTF_8))
        assertContentEquals(expected, KeySchedule.transcriptFrom(hello, challenge))
        assertEquals(
            "f05999c5a87e965fc4725fdab311051d1075bc59bbd39288a444e5e3eb78bf1d",
            Hex.encode(KeySchedule.transcriptFrom(hello, challenge)),
        )
    }

    @Test
    fun goldenProofs() {
        val keys = KeySchedule.keys(master, clientNonce, serverNonce)
        val transcript = KeySchedule.transcriptFrom(hello, challenge)

        val phone = KeySchedule.clientProof(keys.kAuth, clientNonce, serverNonce, transcript)
        val obs = KeySchedule.serverProof(keys.kAuth, serverNonce, clientNonce, transcript)

        assertEquals(32, phone.size)
        assertEquals(32, obs.size)
        assertEquals("a261ecb4c3d7941cf9d88ef4b35b6b6c467e97db2477df31673a4e9a6209586c", Hex.encode(phone))
        assertEquals("226fed4045bf328226a82f26dcded325f7db401705ec156a8eb98bc1bdcca59a", Hex.encode(obs))
        assertTrue(!phone.contentEquals(obs))
    }

    @Test
    fun proofsBindToNonceOrderAndTranscript() {
        val keys = KeySchedule.keys(master, clientNonce, serverNonce)
        val transcript = KeySchedule.transcriptFrom(hello, challenge)
        val phone = KeySchedule.clientProof(keys.kAuth, clientNonce, serverNonce, transcript)

        val swapped = KeySchedule.clientProof(keys.kAuth, serverNonce, clientNonce, transcript)
        assertTrue(!phone.contentEquals(swapped))

        val otherTranscript = KeySchedule.transcriptFrom(hello, """{"type":"challenge2"}""")
        val rebound = KeySchedule.clientProof(keys.kAuth, clientNonce, serverNonce, otherTranscript)
        assertTrue(!phone.contentEquals(rebound))
    }
}
