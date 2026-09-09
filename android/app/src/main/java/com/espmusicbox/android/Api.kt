package com.espmusicbox.android

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import okhttp3.MediaType
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody
import okhttp3.RequestBody.Companion.toRequestBody
import okio.BufferedSink
import java.io.IOException
import java.util.concurrent.TimeUnit

/** 与固件 main/web_api/web_api.c 完全一致的 HTTP API + main/usb_serial 串口 JSON-Lines 协议的
 *  统一接口。JSON 字段使用与设备相同的 snake_case 命名。 */
class ApiException(message: String) : IOException(message)

interface DevApi {
    suspend fun info(): DeviceInfo
    suspend fun status(): DeviceStatus
    suspend fun config(): AppConfig
    suspend fun putConfig(config: AppConfig): OkResponse
    suspend fun files(): List<AudioFile>
    suspend fun upload(name: String, bytes: ByteArray, progress: (Long, Long) -> Unit): OkResponse
    suspend fun delete(name: String): OkResponse
    suspend fun play(name: String, loop: Boolean): OkResponse
    suspend fun stop(): OkResponse
    suspend fun ping(): String
    suspend fun diagnostics(): Diagnostics
    suspend fun sine(seconds: Int, bits: Int): OkResponse
    suspend fun ota(bytes: ByteArray, progress: (Long, Long) -> Unit): OkResponse
    suspend fun setBaud(baud: Int): OkResponse
    suspend fun close()
}

val OCTET_MEDIA = "application/octet-stream".toMediaType()
val JSON_MEDIA = "application/json".toMediaType()

/** HTTP 传输实现（同一个 OkHttpClient/HTTP 接口） */
class HttpApi(private val baseUrl: String) : DevApi {

    private val json = Json { ignoreUnknownKeys = true }
    private val client = OkHttpClient.Builder()
        .connectTimeout(5, TimeUnit.SECONDS)
        .readTimeout(20, TimeUnit.SECONDS)
        .writeTimeout(60, TimeUnit.SECONDS)
        .build()

    private val base = baseUrl.trim().trimEnd('/')
        .ifEmpty { throw IllegalArgumentException("base URL 为空") }

    private fun url(path: String): String = base + path

    private fun <T> call(request: Request, parse: (String) -> T): T {
        try {
            client.newCall(request).execute().use { resp ->
                val body = resp.body?.string() ?: ""
                if (!resp.isSuccessful) {
                    val err = try {
                        json.decodeFromString<ErrorResponse>(body).error
                    } catch (_: Exception) {
                        body.take(200)
                    }
                    throw ApiException("HTTP ${resp.code}: $err".trim())
                }
                return parse(body)
            }
        } catch (e: ApiException) {
            throw e
        } catch (e: IOException) {
            throw ApiException(e.message ?: "网络错误")
        }
    }

    override suspend fun info(): DeviceInfo = withContext(Dispatchers.IO) {
        call(Request.Builder().url(url("/api/info")).get().build()) {
            json.decodeFromString<DeviceInfo>(it)
        }
    }

    override suspend fun status(): DeviceStatus = withContext(Dispatchers.IO) {
        call(Request.Builder().url(url("/api/status")).get().build()) {
            json.decodeFromString<DeviceStatus>(it)
        }
    }

    override suspend fun config(): AppConfig = withContext(Dispatchers.IO) {
        call(Request.Builder().url(url("/api/config")).get().build()) {
            json.decodeFromString<AppConfig>(it)
        }
    }

    override suspend fun putConfig(config: AppConfig): OkResponse = withContext(Dispatchers.IO) {
        val body = json.encodeToString(AppConfig.serializer(), config).toRequestBody(JSON_MEDIA)
        call(Request.Builder().url(url("/api/config")).put(body).build()) {
            json.decodeFromString<OkResponse>(it)
        }
    }

    override suspend fun files(): List<AudioFile> = withContext(Dispatchers.IO) {
        call(Request.Builder().url(url("/api/files")).get().build()) {
            json.decodeFromString<List<AudioFile>>(it)
        }
    }

    override suspend fun upload(name: String, bytes: ByteArray, progress: (Long, Long) -> Unit): OkResponse =
        withContext(Dispatchers.IO) {
            val requestUrl = base + "/api/files?name=" + name.encodeUrlQuery()
            val body = ProgressRequestBody(bytes, OCTET_MEDIA, progress)
            call(Request.Builder().url(requestUrl).post(body).build()) {
                json.decodeFromString<OkResponse>(it)
            }
        }

    override suspend fun delete(name: String): OkResponse = withContext(Dispatchers.IO) {
        val requestUrl = base + "/api/files?name=" + name.encodeUrlQuery()
        call(Request.Builder().url(requestUrl).delete().build()) {
            json.decodeFromString<OkResponse>(it)
        }
    }

    override suspend fun play(name: String, loop: Boolean): OkResponse = withContext(Dispatchers.IO) {
        val escaped = name.replace("\\", "\\\\").replace("\"", "\\\"")
        val payload = """{"name":"$escaped","loop":$loop}""".toRequestBody(JSON_MEDIA)
        call(Request.Builder().url(url("/api/play")).post(payload).build()) {
            json.decodeFromString<OkResponse>(it)
        }
    }

    override suspend fun stop(): OkResponse = withContext(Dispatchers.IO) {
        call(Request.Builder().url(url("/api/stop")).post(ByteArray(0).toRequestBody(null)).build()) {
            json.decodeFromString<OkResponse>(it)
        }
    }

    override suspend fun ping(): String = withContext(Dispatchers.IO) { info().deviceName }

    override suspend fun diagnostics(): Diagnostics = throw ApiException("仅串口可用")
    override suspend fun sine(seconds: Int, bits: Int): OkResponse = throw ApiException("仅串口可用")
    override suspend fun ota(bytes: ByteArray, progress: (Long, Long) -> Unit): OkResponse = throw ApiException("仅串口可用")
    override suspend fun setBaud(baud: Int): OkResponse = throw ApiException("仅串口可用")
    override suspend fun close() = Unit

    private fun String.encodeUrlQuery(): String =
        java.net.URLEncoder.encode(this, Charsets.UTF_8.name()).replace("+", "%20")

    private class ProgressRequestBody(
        private val bytes: ByteArray,
        private val media: MediaType,
        private val onProgress: (Long, Long) -> Unit,
    ) : RequestBody() {
        override fun contentType(): MediaType = media
        override fun contentLength(): Long = bytes.size.toLong()
        override fun writeTo(sink: BufferedSink) {
            var sent = 0L
            val buffer = ByteArray(8192)
            var offset = 0
            while (offset < bytes.size) {
                val n = minOf(buffer.size, bytes.size - offset)
                bytes.copyInto(buffer, 0, offset, offset + n)
                sink.write(buffer, 0, n)
                offset += n
                sent += n
                onProgress(sent, bytes.size.toLong())
            }
        }
    }
}
