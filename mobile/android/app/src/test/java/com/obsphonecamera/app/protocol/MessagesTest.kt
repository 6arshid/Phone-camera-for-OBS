package com.obsphonecamera.app.protocol

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class MessagesTest {

    @Test
    fun discoveryAnnouncementParses() {
        val d = DiscoveryAnnouncement.parse(
            """{"t":"obs","v":1,"id":"00112233445566778899aabbccddeeff","name":"DESKTOP-NAME","tcp":47100,"udp":47109}""",
        )!!
        assertEquals("00112233445566778899aabbccddeeff", d.id)
        assertEquals("DESKTOP-NAME", d.name)
        assertEquals(47100, d.tcpPort)
        assertEquals(47109, d.udpPort)
    }

    @Test
    fun discoveryAnnouncementRejectsInvalidDatagrams() {
        assertNull(DiscoveryAnnouncement.parse("not json"))
        assertNull(DiscoveryAnnouncement.parse("""{"nofield":1}"""))
        assertNull(DiscoveryAnnouncement.parse("""{"t":"phone","v":1,"id":"00112233445566778899aabbccddeeff","tcp":47100}"""))
        assertNull(DiscoveryAnnouncement.parse("""{"t":"obs","v":2,"id":"00112233445566778899aabbccddeeff","tcp":47100}"""))
        assertNull(DiscoveryAnnouncement.parse("""{"t":"obs","v":1,"id":"tooshort","tcp":47100}"""))
        assertNull(DiscoveryAnnouncement.parse("""{"t":"obs","v":1,"id":"00112233445566778899aabbccddeeff","tcp":0}"""))
        assertNull(DiscoveryAnnouncement.parse("""{"t":"obs","v":1,"id":"00112233445566778899aabbccddeeff","tcp":70000}"""))
    }

    @Test
    fun discoveryAnnouncementTruncatesLongName() {
        val longName = "x".repeat(100)
        val d = DiscoveryAnnouncement.parse(
            """{"t":"obs","v":1,"id":"00112233445566778899aabbccddeeff","name":"$longName","tcp":47100,"udp":47109}""",
        )!!
        assertEquals(64, d.name.length)
    }

    @Test
    fun streamConfigParsesFullSpecExample() {
        val cfg = StreamConfig.parse(
            """
            {"type":"config",
             "video":{"codec":"h264","width":1920,"height":1080,"fps":30,
                      "bitrate_kbps":4000,"gop_s":2,"camera":"back",
                      "orientation":"auto"},
             "camera":{"focus_mode":"auto","focus_distance":0.5,
                       "exposure_mode":"auto","exposure_bias":0.0,
                       "wb_mode":"auto","wb_kelvin":5500,"torch":false},
             "audio":{"enabled":false,"sample_rate":48000,"channels":1},
             "stream":{"active":true}}
            """.trimIndent(),
        )

        assertEquals("h264", cfg.video.codec)
        assertEquals(1920, cfg.video.width)
        assertEquals(1080, cfg.video.height)
        assertEquals(30, cfg.video.fps)
        assertEquals(4000, cfg.video.bitrateKbps)
        assertEquals(2, cfg.video.gopS)
        assertEquals("back", cfg.video.camera)
        assertEquals("auto", cfg.video.orientation)
        assertEquals("auto", cfg.camera.focusMode)
        assertEquals(0.5f, cfg.camera.focusDistance)
        assertEquals("auto", cfg.camera.exposureMode)
        assertEquals(0.0f, cfg.camera.exposureBias)
        assertEquals("auto", cfg.camera.wbMode)
        assertEquals(5500, cfg.camera.wbKelvin)
        assertFalse(cfg.camera.torch)
        assertFalse(cfg.audio.enabled)
        assertEquals(48000, cfg.audio.sampleRate)
        assertEquals(1, cfg.audio.channels)
        assertTrue(cfg.active)
    }

    @Test
    fun streamConfigPartialConfigUsesDefaults() {
        val cfg = StreamConfig.parse("""{"type":"config","video":{"camera":"front"}}""")
        assertEquals("front", cfg.video.camera)
        assertEquals(1920, cfg.video.width)
        assertEquals(1080, cfg.video.height)
        assertEquals(30, cfg.video.fps)
        assertEquals("h264", cfg.video.codec)
        assertEquals("auto", cfg.camera.focusMode)
        assertEquals(5500, cfg.camera.wbKelvin)
        assertFalse(cfg.audio.enabled)
        assertFalse(cfg.active)
    }

    @Test
    fun streamConfigClampsOutOfRangeValues() {
        val cfg = StreamConfig.parse(
            """
            {"type":"config",
             "video":{"width":99999,"height":1,"fps":0,"bitrate_kbps":300000,"gop_s":0},
             "camera":{"focus_distance":5.0,"exposure_bias":-9.0,"wb_kelvin":100},
             "stream":{"active":true}}
            """.trimIndent(),
        )
        assertEquals(3840, cfg.video.width)
        assertEquals(120, cfg.video.height)
        assertEquals(1, cfg.video.fps)
        assertEquals(200000, cfg.video.bitrateKbps)
        assertEquals(1, cfg.video.gopS)
        assertEquals(1.0f, cfg.camera.focusDistance)
        assertEquals(-2.0f, cfg.camera.exposureBias)
        assertEquals(2500, cfg.camera.wbKelvin)
    }

    @Test
    fun streamConfigIgnoresUnknownKeys() {
        val cfg = StreamConfig.parse(
            """{"type":"config","unknown":{"x":1},"video":{"camera":"toggle","extra":true},"new_field":"zzz"}""",
        )
        assertEquals("toggle", cfg.video.camera)
        assertFalse(cfg.active)
    }

    @Test
    fun hintMessageParses() {
        val h = HintMessage.parse("""{"type":"hint","loss_pct":2.5,"rtt_ms":12,"queue_ms":30}""")
        assertEquals(2.5f, h.lossPct)
        assertEquals(12, h.rttMs)
        assertEquals(30, h.queueMs)
    }

    @Test
    fun authOkParses() {
        val ok = AuthOk.parse(
            """{"type":"auth_ok","session":"00112233445566778899aabbccddeeff","media_port":47101,"proof":"${"ab".repeat(32)}","paired":true}""",
        )
        assertEquals("00112233445566778899aabbccddeeff", ok.sessionHex)
        assertEquals(47101, ok.mediaPort)
        assertEquals("ab".repeat(32), ok.proofHex)
        assertTrue(ok.paired)
    }

    @Test
    fun authOkDefaultsPairedFalse() {
        val ok = AuthOk.parse(
            """{"type":"auth_ok","session":"${"00".repeat(16)}","media_port":47101,"proof":"${"ab".repeat(32)}"}""",
        )
        assertFalse(ok.paired)
    }

    @Test
    fun authOkRejectsInvalidFields() {
        assertFailsWith<ProtocolException> {
            AuthOk.parse("""{"type":"auth_ok","session":"aabb","media_port":47101,"proof":"${"ab".repeat(32)}"}""")
        }
        assertFailsWith<ProtocolException> {
            AuthOk.parse("""{"type":"auth_ok","session":"${"00".repeat(16)}","media_port":0,"proof":"${"ab".repeat(32)}"}""")
        }
        assertFailsWith<ProtocolException> {
            AuthOk.parse("""{"type":"auth_ok","session":"${"00".repeat(16)}","media_port":70000,"proof":"${"ab".repeat(32)}"}""")
        }
        assertFailsWith<ProtocolException> {
            AuthOk.parse("""{"type":"auth_ok","session":"${"00".repeat(16)}","media_port":47101,"proof":"ccdd"}""")
        }
    }
}
