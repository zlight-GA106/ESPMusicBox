package com.espmusicbox.android

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.IOException
import java.util.concurrent.TimeUnit

/** 与固件 main/web_api/web_api.c 完全一致的 HTTP API 客户端。
 *  JSON 字段使用与设备相同的 snake_case 命名。 */
class ApiException(message: String) : IOException(message)

class DeviceApi(private val baseUrl: String) {

    private val json = Json { ignoreUnknownKeys = true }
    private val jsonMedia = "application/json".toMediaType()
    private val octetMedia = "application/octet-stream".toMediaType()

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

    suspend fun getInfo(): DeviceInfo = withContext(Dispatchers.IO) {
        call(Request.Builder().url(url("/api/info")).get().build()) {
            json.decodeFromString<DeviceInfo>(it)
        }
    }

    suspend fun getStatus(): DeviceStatus = withContext(Dispatchers.IO) {
        call(Request.Builder().url(url("/api/status")).get().build()) {
            json.decodeFromString<DeviceStatus>(it)
        }
    }

    suspend fun getConfig(): AppConfig = withContext(Dispatchers.IO) {
        call(Request.Builder().url(url("/api/config")).get().build()) {
            json.decodeFromString<AppConfig>(it)
        }
    }

    suspend fun putConfig(config: AppConfig): OkResponse = withContext(Dispatchers.IO) {
        val body = json.encodeToString(AppConfig.serializer(), config).toRequestBody(jsonMedia)
        call(Request.Builder().url(url("/api/config")).put(body).build()) {
            json.decodeFromString<OkResponse>(it)
        }
    }

    suspend fun getFiles(): List<AudioFile> = withContext(Dispatchers.IO) {
        call(Request.Builder().url(url("/api/files")).get().build()) {
            json.decodeFromString<List<AudioFile>>(it)
        }
    }

    suspend fun uploadFile(name: String, bytes: ByteArray): OkResponse = withContext(Dispatchers.IO) {
        val requestUrl = base + "/api/files?name=" + name.encodeUrlQuery()
        val body = bytes.toRequestBody(octetMedia, 0, bytes.size)
        call(Request.Builder().url(requestUrl).post(body).build()) {
            json.decodeFromString<OkResponse>(it)
        }
    }

    suspend fun deleteFile(name: String): OkResponse = withContext(Dispatchers.IO) {
        val requestUrl = base + "/api/files?name=" + name.encodeUrlQuery()
        call(Request.Builder().url(requestUrl).delete().build()) {
            json.decodeFromString<OkResponse>(it)
        }
    }

    suspend fun play(name: String, loop: Boolean): OkResponse = withContext(Dispatchers.IO) {
        val escaped = name.replace("\\", "\\\\").replace("\"", "\\\"")
        val payload = """{"name":"$escaped","loop":$loop}""".toRequestBody(jsonMedia)
        call(Request.Builder().url(url("/api/play")).post(payload).build()) {
            json.decodeFromString<OkResponse>(it)
        }
    }

    suspend fun stop(): OkResponse = withContext(Dispatchers.IO) {
        call(Request.Builder().url(url("/api/stop")).post(ByteArray(0).toRequestBody(null)).build()) {
            json.decodeFromString<OkResponse>(it)
        }
    }

    private fun String.encodeUrlQuery(): String =
        java.net.URLEncoder.encode(this, Charsets.UTF_8.name()).replace("+", "%20")
}
