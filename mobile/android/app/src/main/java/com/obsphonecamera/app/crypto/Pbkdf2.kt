package com.obsphonecamera.app.crypto

import java.security.spec.KeySpec
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.PBEKeySpec

object Pbkdf2 {

    fun derive(password: ByteArray, salt: ByteArray, iterations: Int, dkLen: Int): ByteArray {
        val chars = CharArray(password.size) { i -> password[i].toChar() }
        val spec: KeySpec = PBEKeySpec(chars, salt, iterations, dkLen * 8)
        return SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256").generateSecret(spec).encoded
    }
}
