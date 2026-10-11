package com.obsphonecamera.app.crypto

import com.obsphonecamera.app.util.Hex
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertNotEquals

class Pbkdf2Test {

    private fun run(password: String, salt: String, iter: Int, dkLen: Int): String =
        Hex.encode(
            Pbkdf2.derive(
                password.toByteArray(Charsets.US_ASCII),
                salt.toByteArray(Charsets.US_ASCII),
                iter,
                dkLen,
            ),
        )

    @Test
    fun knownVectors() {
        assertEquals(
            "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b",
            run("password", "salt", 1, 32),
        )
        assertEquals(
            "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43",
            run("password", "salt", 2, 32),
        )
        assertEquals(
            "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a",
            run("password", "salt", 4096, 32),
        )
        assertEquals(
            "348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1c635518c7dac47e9",
            run("passwordPASSWORDpassword", "saltSALTsaltSALTsaltSALTsaltSALTsalt", 4096, 40),
        )
        assertEquals(
            "986c856b216297f7c696d0a8e67e8c1ead27fab3e5e9557d15e3a87970aa2695",
            run("password", "NaCl1234", 1000, 32),
        )
    }

    @Test
    fun pairingCodeDerivationSensitivity() {
        val salt = ByteArray(16) { 0x5a }
        val a = Pbkdf2.derive("123456".toByteArray(Charsets.US_ASCII), salt, 100000, 32)
        val b = Pbkdf2.derive("123456".toByteArray(Charsets.US_ASCII), salt, 100000, 32)
        val c = Pbkdf2.derive("654321".toByteArray(Charsets.US_ASCII), salt, 100000, 32)
        val d = Pbkdf2.derive("123456".toByteArray(Charsets.US_ASCII), salt, 100001, 32)
        val e = Pbkdf2.derive("123456".toByteArray(Charsets.US_ASCII), ByteArray(16) { 0x5b }, 100000, 32)

        assertContentEquals(a, b)
        assertNotEquals(Hex.encode(a), Hex.encode(c))
        assertNotEquals(Hex.encode(a), Hex.encode(d))
        assertNotEquals(Hex.encode(a), Hex.encode(e))
    }
}
