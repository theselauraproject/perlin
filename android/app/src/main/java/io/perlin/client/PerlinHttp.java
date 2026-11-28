package io.perlin.client;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.zip.GZIPInputStream;
import java.util.zip.InflaterInputStream;

/**
 * HTTP(S) for the native game through the platform stack (Conscrypt TLS, the system and user CA store,
 * Private DNS, per-app VPNs and proxies, network security config). The native side calls these static
 * methods over JNI from its own worker threads and falls back to its OpenSSL client only when this fails.
 */
public final class PerlinHttp {
    private static final int MAX_REDIRECTS = 8;

    private PerlinHttp() {
    }

    /**
     * One request. Returns {Integer status, byte[] body, String[] headers (name, value, ...), String error}.
     * A status of 0 means the request did not complete (DNS, connect, TLS, timeout); error says why.
     */
    public static Object[] request(String url, String method, String[] headers, byte[] body, int timeoutMs) {
        HttpURLConnection connection = null;
        try {
            String current = url;
            String currentMethod = method;
            byte[] currentBody = body;
            for (int hop = 0; ; hop++) {
                connection = open(current, currentMethod, headers, timeoutMs);
                if (currentBody != null && currentBody.length > 0 && !"GET".equals(currentMethod)) {
                    connection.setDoOutput(true);
                    connection.setFixedLengthStreamingMode(currentBody.length);
                    try (OutputStream out = connection.getOutputStream()) {
                        out.write(currentBody);
                    }
                }
                final int status = connection.getResponseCode();
                final String location = connection.getHeaderField("Location");
                if (isRedirect(status) && location != null && hop < MAX_REDIRECTS) {
                    current = new URL(new URL(current), location).toString();
                    // Browsers and cpp-httplib turn a redirected POST into a GET for 301, 302 and 303.
                    if (status == 303 || ((status == 301 || status == 302) && "POST".equals(currentMethod))) {
                        currentMethod = "GET";
                        currentBody = null;
                    }
                    connection.disconnect();
                    connection = null;
                    continue;
                }
                return response(connection, status);
            }
        } catch (Throwable t) {
            return new Object[] {0, new byte[0], new String[0], describe(t)};
        } finally {
            if (connection != null)
                connection.disconnect();
        }
    }

    /**
     * Streams a GET to a file. `state` is native memory: long done at 0, long total at 8, and a cancel
     * flag byte at 16 that the native side may set at any time. Returns null on success, else the reason.
     */
    public static String download(String url, String path, ByteBuffer state, int timeoutMs) {
        HttpURLConnection connection = null;
        if (state != null)
            state.order(ByteOrder.nativeOrder());
        try {
            String current = url;
            for (int hop = 0; ; hop++) {
                connection = open(current, "GET", new String[] {"User-Agent", "Perlin"}, timeoutMs);
                final int status = connection.getResponseCode();
                final String location = connection.getHeaderField("Location");
                if (isRedirect(status) && location != null && hop < MAX_REDIRECTS) {
                    current = new URL(new URL(current), location).toString();
                    connection.disconnect();
                    connection = null;
                    continue;
                }
                if (status != 200)
                    return "HTTP " + status;
                final long total = connection.getContentLengthLong();
                if (state != null)
                    state.putLong(8, Math.max(total, 0));
                final File target = new File(path);
                final File parent = target.getParentFile();
                if (parent != null)
                    parent.mkdirs();
                long done = 0;
                try (InputStream in = decoded(connection, connection.getInputStream());
                     OutputStream out = new FileOutputStream(target)) {
                    final byte[] buffer = new byte[1 << 16];
                    int n;
                    while ((n = in.read(buffer)) > 0) {
                        if (state != null && state.get(16) != 0)
                            return "cancelled";
                        out.write(buffer, 0, n);
                        done += n;
                        if (state != null)
                            state.putLong(0, done);
                    }
                }
                if (total > 0 && done != total)
                    return "connection closed after " + done + " of " + total + " bytes";
                return null;
            }
        } catch (Throwable t) {
            return describe(t);
        } finally {
            if (connection != null)
                connection.disconnect();
        }
    }

    private static HttpURLConnection open(String url, String method, String[] headers, int timeoutMs)
            throws IOException {
        final HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setInstanceFollowRedirects(false);
        connection.setConnectTimeout(timeoutMs);
        connection.setReadTimeout(timeoutMs);
        connection.setUseCaches(false);
        connection.setRequestMethod(method);
        if (headers != null)
            for (int i = 0; i + 1 < headers.length; i += 2)
                connection.setRequestProperty(headers[i], headers[i + 1]);
        return connection;
    }

    private static boolean isRedirect(int status) {
        return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    }

    private static Object[] response(HttpURLConnection connection, int status) throws IOException {
        InputStream stream;
        try {
            stream = connection.getInputStream();
        } catch (IOException e) {
            stream = connection.getErrorStream();
        }
        byte[] body = new byte[0];
        if (stream != null) {
            try (InputStream in = decoded(connection, stream)) {
                final ByteArrayOutputStream out = new ByteArrayOutputStream(Math.max(1024,
                        connection.getContentLength()));
                final byte[] buffer = new byte[1 << 14];
                int n;
                while ((n = in.read(buffer)) > 0)
                    out.write(buffer, 0, n);
                body = out.toByteArray();
            }
        }
        final List<String> flat = new ArrayList<>();
        final boolean decoded = encoding(connection) != null;
        for (Map.Entry<String, List<String>> entry : connection.getHeaderFields().entrySet()) {
            final String name = entry.getKey();
            if (name == null || entry.getValue() == null)
                continue;
            // The body handed back is already decoded, so its encoding and length no longer apply.
            if (decoded && ("content-encoding".equalsIgnoreCase(name) || "content-length".equalsIgnoreCase(name)))
                continue;
            for (String value : entry.getValue()) {
                flat.add(name);
                flat.add(value == null ? "" : value);
            }
        }
        return new Object[] {status, body, flat.toArray(new String[0]), null};
    }

    // HttpURLConnection only decompresses by itself when it added Accept-Encoding; a caller that asked
    // for gzip itself gets the raw stream, which is decoded here so the native side always sees plain bytes.
    private static String encoding(HttpURLConnection connection) {
        final String value = connection.getHeaderField("Content-Encoding");
        if (value == null)
            return null;
        final String lower = value.trim().toLowerCase(java.util.Locale.ROOT);
        return lower.equals("gzip") || lower.equals("deflate") ? lower : null;
    }

    private static InputStream decoded(HttpURLConnection connection, InputStream in) throws IOException {
        final String encoding = encoding(connection);
        if ("gzip".equals(encoding))
            return new GZIPInputStream(in);
        if ("deflate".equals(encoding))
            return new InflaterInputStream(in);
        return in;
    }

    private static String describe(Throwable t) {
        final String message = t.getMessage();
        return t.getClass().getSimpleName() + (message != null ? ": " + message : "");
    }
}
