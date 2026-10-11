package com.obsphonecamera.app.protocol

import com.obsphonecamera.app.util.Hex
import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class AvatarProtocolTest {

    private fun sha(bytes: ByteArray) = Hex.encode(MessageDigest.getInstance("SHA-256").digest(bytes))

    private fun chunk(id: String, offset: Int, total: Int, data: ByteArray) =
        """{"type":"avatar_chunk","id":"$id","offset":$offset,"total":$total,"data":"${Hex.encode(data)}"}"""

    @Test
    fun avatarConfigParsesAndClamps() {
        val legacy = StreamConfig.parse("""{"type":"config"}""")
        assertFalse(legacy.avatar.enabled)
        assertFalse(legacy.avatar.hasImage)

        val cfg = StreamConfig.parse(
            """{"avatar":{"enabled":true,"image":"${"AB".repeat(32)}","size":1000,"mode":"position",
               "cols":40,"rows":0,"fps":99,"scale":10,"offset_y":-500,"flip":true}}""",
        ).avatar
        assertTrue(cfg.hasImage)
        assertEquals("ab".repeat(32), cfg.image)
        assertEquals("position", cfg.mode)
        assertEquals(16, cfg.cols)
        assertEquals(1, cfg.rows)
        assertEquals(30, cfg.fps)
        assertEquals(50, cfg.scale)
        assertEquals(-100, cfg.offsetY)
        assertTrue(cfg.flip)

        val badHash = StreamConfig.parse("""{"avatar":{"enabled":true,"image":"xyz","size":10}}""").avatar
        assertFalse(badHash.hasImage)
    }

    @Test
    fun assemblerJoinsChunksAndVerifiesHash() {
        val file = ByteArray(70_000) { (it * 31).toByte() }
        val id = sha(file)
        val asm = AvatarAssembler()
        assertNull(asm.accept(chunk(id, 0, file.size, file.copyOfRange(0, 30_000))))
        assertNull(asm.accept(chunk(id, 30_000, file.size, file.copyOfRange(30_000, 60_000))))
        val done = asm.accept(chunk(id, 60_000, file.size, file.copyOfRange(60_000, file.size)))
        assertNotNull(done)
        assertEquals(id, done.first)
        assertContentEquals(file, done.second)
    }

    @Test
    fun assemblerRejectsCorruptOrOutOfOrderData() {
        val file = ByteArray(100) { it.toByte() }
        val id = sha(file)
        val asm = AvatarAssembler()
        // Wrong content for the announced hash.
        assertNull(asm.accept(chunk(id, 0, file.size, ByteArray(100))))
        // Gap: second chunk arrives without the first.
        assertNull(asm.accept(chunk(id, 50, file.size, file.copyOfRange(50, 100))))
        // A restart at offset 0 recovers.
        assertNull(asm.accept(chunk(id, 0, file.size, file.copyOfRange(0, 50))))
        assertNotNull(asm.accept(chunk(id, 50, file.size, file.copyOfRange(50, 100))))
        // Oversized totals are refused outright.
        assertNull(asm.accept(chunk(id, 0, AvatarConfig.MAX_BYTES + 1, file)))
    }
}
