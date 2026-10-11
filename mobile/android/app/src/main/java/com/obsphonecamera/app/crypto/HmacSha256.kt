package com.obsphonecamera.app.crypto

import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

object HmacSha256 {

    fun mac(key: ByteArray, vararg parts: ByteArray): ByteArray {
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(key, "HmacSHA256"))
        for (p in parts) {
            mac.update(p)
        }
        return mac.doFinal()
    }
}
