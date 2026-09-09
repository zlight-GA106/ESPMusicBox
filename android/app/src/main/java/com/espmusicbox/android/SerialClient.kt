package com.espmusicbox.android

import android.content.Context
import android.content.Intent
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.decodeFromJsonElement
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.longOrNull
import kotlinx.serialization.json.put
import java.io.IOException
import java.util.Base64
import java.util.concurrent.atomic.AtomicLong

const val USB_PERMISSION_ACTION = "com.espmusicbox.android.USB_PERMISSION"

/** USB 主机模式设备发现与授权。串口传输配置模式的事 1:1 照搬 pc_tool 串口通道。
 *  支持的芯片与固件 USB CDC / CH34x 一致：usb-serial-for-android 默认探测器
 *  覆盖 CDC-ACM、CH340/CH341、CP210x、FTDI、PL2303。 */
object UsbSerialUtils {

    fun listDevices(context: Context): List<UsbDevice> {
        val manager = context.getSystemService(Context.USB_SERVICE) as UsbManager
        return UsbSerialProber.getDefaultProber().findAllDrivers(manager).map { it.device }
    }

    fun portOf(context: Context, device: UsbDevice): UsbSerialPort {
        val manager = context.getSystemService(Context.USB_SERVICE) as UsbManager
        return UsbSerialProber.getDefaultProber().findAllDrivers(manager)
            .firstOrNull { it.device.deviceId == device.deviceId }?.ports?.firstOrNull()
            ?: throw IOException("未找到该设备的串口驱动接口")
    }

    fun hasPermission(context: Context, device: UsbDevice): Boolean {
        val manager = context.getSystemService(Context.USB_SERVICE) as UsbManager
        return manager.hasPermission(device)
    }

    fun requestPermission(context: Context, device: UsbDevice) {
        val manager = context.getSystemService(Context.USB_SERVICE) as UsbManager
        val pi = android.app.PendingIntent.getBroadcast(
            context, device.deviceId,
            Intent(USB_PERMISSION_ACTION).setPackage(context.packageName),
            android.app.PendingIntent.FLAG_UPDATE_CURRENT or android.app.PendingIntent.FLAG_MUTABLE
        )
        manager.requestPermission(device, pi)
    }

    fun deviceLabel(device: UsbDevice): String = buildString {
        append(device.productName ?: "USB 串口")
        append(" (VID %04X PID %04X)".format(device.vendorId, device.productId))
    }
}

/** 串口 JSON-Lines 协议实现（docs/PROTOCOL.md）：
 *  请求 {"id":N,"cmd":"CMD","data":{}}；响应 {"id":N,"ok":true,"data":...} 或
 *  {"id":N,"ok":false,"error":"..."}。一条请求只等相同 id 的响应。 */
class SerialTransport(
    private val context: Context,
    private val device: UsbDevice,
    private val baud: Int = 115200,
) : DevApi {

    companion object {
        const val RAW_CHUNK = 2048           // 每块原始数据 ≤ 2880（协议上限），取 2048 安全
        val BAUD_OPTIONS = listOf(115200, 230400, 460800, 921600, 1500000, 2000000)
    }

    private val json = Json { ignoreUnknownKeys = true }
    private val idGen = AtomicLong(0)
    private var port: UsbSerialPort? = null
    private var currentBaud: Int = baud

    @Synchronized
    fun open(): SerialTransport {
        val p = UsbSerialUtils.portOf(context, device)
        val running = context.getSystemService(Context.USB_SERVICE) as UsbManager
        val conn = running.openDevice(device)
            ?: throw IOException("无法打开该设备的 USB 外设连接")
        try {
            p.open(conn)
            try {
                p.setParameters(currentBaud, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
            } catch (_: Exception) {
                // USB CDC 忽略波特率；CH34x/CP210x 生效
            }
            port = p
            // 握手探测：确认对端是本项目固件
            val hello = request("ping").toString().parseGreeting()
            if (hello != "pong") throw ApiException("设备未响应 ping（协议不匹配，确认固件为音乐盒）")
        } catch (e: Exception) {
            runCatching { p.close() }
            port = null
            throw if (e is ApiException) e else ApiException(e.message ?: "串口打开失败")
        }
        return this
    }

    private fun String.parseGreeting(): String = trim().trim('"').lowercase()

    /** 行读取：USB 主机的 CDC/CH34x 批量读超时语义各异，手动按行缓冲并限时 */
    private fun nextLine(timeoutMs: Long = 5000): String? {
        val p = port ?: throw ApiException("串口未打开")
        val out = java.io.ByteArrayOutputStream(256)
        val buf = ByteArray(256)
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            val n = try {
                p.read(buf, minOf(250, 1000).toInt())
            } catch (_: Exception) {
                -1
            }
            if (n == -1 || n == 0) continue
            for (i in 0 until n) {
                if (buf[i] == '\n'.code.toByte()) {
                    return String(out.toByteArray(), Charsets.UTF_8)
                }
                out.write(buf[i].toInt())
                if (out.size() > 4095) return null
            }
        }
        return null
    }

    @Synchronized
    private fun request(cmd: String, data: JsonObject? = null): kotlinx.serialization.json.JsonElement {
        val id = idGen.incrementAndGet()
        val line = buildJsonObject {
            put("id", id)
            put("cmd", cmd)
            put("data", data ?: JsonObject(emptyMap()))
        }.toString() + "\n"
        val p = port ?: throw ApiException("串口未打开")
        try {
            p.write(line.toByteArray(Charsets.UTF_8), 5000)
        } catch (e: IOException) {
            throw ApiException("串口写入失败: ${e.message}")
        }
        while (true) {
            val respLine = nextLine() ?: throw ApiException("串口读取超时或连接已断开")
            val el = try {
                json.parseToJsonElement(respLine)
            } catch (_: Exception) {
                continue
            }
            val obj = el as? JsonObject ?: continue
            if (obj["id"]?.jsonPrimitive?.longOrNull != id) continue
            val ok = obj["ok"]?.jsonPrimitive?.booleanOrNull ?: false
            if (ok) return obj["data"] ?: JsonNull
            throw ApiException(obj["error"]?.jsonPrimitive?.contentOrNull ?: "串口命令失败")
        }
    }

    private inline fun <reified T> decode(el: kotlinx.serialization.json.JsonElement): T =
        json.decodeFromJsonElement<T>(el)

    private suspend fun <T> withIo(block: () -> T): T = withContext(Dispatchers.IO) { block() }

    override suspend fun info(): DeviceInfo = withIo { decode<DeviceInfo>(request("info.get")) }

    override suspend fun status(): DeviceStatus = withIo { decode<DeviceStatus>(request("status.get")) }

    override suspend fun config(): AppConfig = withIo { decode<AppConfig>(request("config.get")) }

    override suspend fun putConfig(config: AppConfig): OkResponse = withIo {
        request("config.set", json.encodeToJsonElement(AppConfig.serializer(), config).jsonObject)
        OkResponse(ok = true)
    }

    override suspend fun files(): List<AudioFile> = withIo {
        decode<List<AudioFile>>(request("file.list"))
    }

    override suspend fun upload(name: String, bytes: ByteArray, progress: (Long, Long) -> Unit): OkResponse =
        withIo {
            request("file.begin", buildJsonObject {
                put("name", name)
                put("size", bytes.size)
            })
            val b64 = Base64.getEncoder()
            var sent = 0L
            var offset = 0
            try {
                while (offset < bytes.size) {
                    val n = minOf(RAW_CHUNK, bytes.size - offset)
                    val chunk = b64.encodeToString(bytes.copyOfRange(offset, offset + n))
                    request("file.chunk", buildJsonObject { put("base64", chunk) })
                    offset += n
                    sent += n
                    progress(sent, bytes.size.toLong())
                }
                request("file.end")
            } catch (e: Exception) {
                runCatching { request("file.abort") }
                throw e
            }
            OkResponse(ok = true)
        }

    override suspend fun delete(name: String): OkResponse = withIo {
        request("file.delete", buildJsonObject { put("name", name) })
        OkResponse(ok = true)
    }

    override suspend fun play(name: String, loop: Boolean): OkResponse = withIo {
        request("audio.play", buildJsonObject {
            put("name", name)
            put("loop", loop)
        })
        OkResponse(ok = true)
    }

    override suspend fun stop(): OkResponse = withIo {
        request("audio.stop")
        OkResponse(ok = true)
    }

    override suspend fun ping(): String = withIo { request("ping").jsonPrimitive.content }

    override suspend fun diagnostics(): Diagnostics = withIo {
        decode<Diagnostics>(request("audio.diagnostics"))
    }

    override suspend fun sine(seconds: Int, bits: Int): OkResponse = withIo {
        request("audio.sine", buildJsonObject {
            put("seconds", seconds)
            put("bits", bits)
        })
        OkResponse(ok = true)
    }

    override suspend fun ota(bytes: ByteArray, progress: (Long, Long) -> Unit): OkResponse = withIo {
        request("ota.begin", buildJsonObject { put("size", bytes.size) })
        val b64 = Base64.getEncoder()
        var sent = 0L
        var offset = 0
        try {
            while (offset < bytes.size) {
                val n = minOf(RAW_CHUNK, bytes.size - offset)
                val chunk = b64.encodeToString(bytes.copyOfRange(offset, offset + n))
                request("ota.chunk", buildJsonObject { put("base64", chunk) })
                offset += n
                sent += n
                progress(sent, bytes.size.toLong())
            }
            request("ota.end")
        } catch (e: Exception) {
            runCatching { request("ota.abort") }
            throw e
        }
        OkResponse(ok = true)
    }

    override suspend fun setBaud(baud: Int): OkResponse = withIo {
        if (baud !in BAUD_OPTIONS) throw ApiException("波特率必须是 ${BAUD_OPTIONS.joinToString("/")}")
        request("uart.baud", buildJsonObject { put("baud", baud) })
        currentBaud = baud
        val p = port ?: throw ApiException("串口未打开")
        try {
            p.setParameters(currentBaud, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
        } catch (_: Exception) {
            // CDC 无该设置，忽略；变更对 CH34x 生效
        }
        OkResponse(ok = true)
    }

    override suspend fun close() = withIo {
        runCatching { port?.close() }
        port = null
    }

    fun baud(): Int = currentBaud
    fun deviceLabel(): String = UsbSerialUtils.deviceLabel(device)
}
