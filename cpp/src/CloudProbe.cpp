#include "CloudProbe.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QPair>
#include <QString>
#include <QtGlobal>

#include <cstring>
#include <cmath>
#include <cerrno>

#if defined(Q_OS_WIN)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#if defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#include <sys/time.h>
#include <ctime>
#endif

namespace {

#if defined(Q_OS_WIN)
using SocketHandle = SOCKET;
constexpr SocketHandle kBadSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kBadSocket = -1;
#endif

struct HttpResponse {
    bool ok = false;
    int status = 0;
    QByteArray body;
    QString error;
};

void closeSocket(SocketHandle fd) {
#if defined(Q_OS_WIN)
    ::closesocket(fd);
#else
    ::close(fd);
#endif
}

int lastSocketError() {
#if defined(Q_OS_WIN)
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

bool connectInProgress(int error) {
#if defined(Q_OS_WIN)
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return error == EINPROGRESS || error == EWOULDBLOCK || error == EINTR;
#endif
}

void setBlocking(SocketHandle fd, bool blocking) {
#if defined(Q_OS_WIN)
    u_long mode = blocking ? 0 : 1;
    ::ioctlsocket(fd, FIONBIO, &mode);
#else
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return;
    }
    ::fcntl(fd, F_SETFL, blocking ? (flags & ~O_NONBLOCK)
                                  : (flags | O_NONBLOCK));
#endif
}

void setRecvTimeout(SocketHandle fd, int timeoutMs) {
#if defined(Q_OS_WIN)
    DWORD tv = DWORD(timeoutMs);
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char *>(&tv), sizeof(tv));
#else
    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

#if defined(Q_OS_WIN)
struct WinsockScope {
    WinsockScope() {
        WSADATA data{};
        started = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockScope() {
        if (started) {
            ::WSACleanup();
        }
    }
    bool started = false;
};
#endif

HttpResponse httpRequest(const QString &host, quint16 port,
                         const QByteArray &method, const QString &path,
                         const QList<QPair<QByteArray, QByteArray>> &headers,
                         int timeoutMs) {
    HttpResponse result;
#if defined(Q_OS_WIN)
    WinsockScope winsock;
    if (!winsock.started) {
        result.error = QStringLiteral("winsock unavailable");
        return result;
    }
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    const QByteArray hostBytes = host.toLatin1();
#if defined(Q_OS_WIN)
    if (::InetPtonA(AF_INET, hostBytes.constData(), &address.sin_addr) != 1) {
#else
    if (::inet_pton(AF_INET, hostBytes.constData(), &address.sin_addr) != 1) {
#endif
        result.error = QStringLiteral("metadata address is not parsable");
        return result;
    }

    const SocketHandle fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kBadSocket) {
        result.error = QStringLiteral("cannot open a socket");
        return result;
    }

    setBlocking(fd, false);
    int rc = ::connect(fd, reinterpret_cast<sockaddr *>(&address),
                       sizeof(address));
    if (rc != 0) {
        if (!connectInProgress(lastSocketError())) {
            closeSocket(fd);
            result.error = QStringLiteral("metadata host unreachable");
            return result;
        }
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(fd, &writable);
        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        const int ready = ::select(int(fd) + 1, nullptr, &writable, nullptr, &tv);
        if (ready <= 0) {
            closeSocket(fd);
            result.error = QStringLiteral("metadata connect timed out");
            return result;
        }
        int soError = 0;
#if defined(Q_OS_WIN)
        int length = sizeof(soError);
        ::getsockopt(fd, SOL_SOCKET, SO_ERROR,
                     reinterpret_cast<char *>(&soError), &length);
#else
        socklen_t length = sizeof(soError);
        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &length);
#endif
        if (soError != 0) {
            closeSocket(fd);
            result.error = QStringLiteral("metadata host refused the connection");
            return result;
        }
    }
    setBlocking(fd, true);
    setRecvTimeout(fd, timeoutMs);

    QByteArray request = method + ' ' + path.toUtf8() + " HTTP/1.0\r\nHost: "
        + hostBytes + "\r\nAccept: */*\r\nConnection: close\r\n";
    for (const auto &header : headers) {
        request += header.first + ": " + header.second + "\r\n";
    }
    request += "\r\n";

    qint64 sent = 0;
    while (sent < request.size()) {
#if defined(Q_OS_WIN)
        const int written = ::send(fd, request.constData() + sent,
                                   int(request.size() - sent), 0);
#else
        const ssize_t written = ::send(fd, request.constData() + sent,
                                       size_t(request.size() - sent), 0);
#endif
        if (written <= 0) {
            break;
        }
        sent += written;
    }
    if (sent != request.size()) {
        closeSocket(fd);
        result.error = QStringLiteral("metadata request was not sent");
        return result;
    }

    QElapsedTimer timer;
    timer.start();
    QByteArray raw;
    char buffer[4096];
    while (raw.size() < 65536 && timer.elapsed() < timeoutMs) {
#if defined(Q_OS_WIN)
        const int got = ::recv(fd, buffer, int(sizeof(buffer)), 0);
#else
        const ssize_t got = ::recv(fd, buffer, sizeof(buffer), 0);
#endif
        if (got == 0) {
            break;
        }
        if (got < 0) {
            break;
        }
        raw.append(buffer, int(got));
    }
    closeSocket(fd);

    const int headerEnd = raw.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        result.error = QStringLiteral("metadata response had no headers");
        return result;
    }
    const QByteArray head = raw.left(headerEnd);
    QByteArray body = raw.mid(headerEnd + 4);
    const QList<QByteArray> headLines = head.split('\n');
    const QList<QByteArray> statusParts = headLines.value(0).trimmed().split(' ');
    result.status = statusParts.value(1).toInt();
    for (const QByteArray &line : headLines) {
        const QByteArray lowered = line.trimmed().toLower();
        if (lowered.startsWith("content-length:")) {
            const int declared =
                lowered.mid(int(std::strlen("content-length:"))).trimmed().toInt();
            if (declared >= 0 && body.size() > declared) {
                body.truncate(declared);
            }
        }
    }
    result.ok = result.status >= 200 && result.status < 300;
    result.body = body;
    return result;
}

QList<QPair<QByteArray, QByteArray>> headers(
    std::initializer_list<QPair<QByteArray, QByteArray>> list) {
    QList<QPair<QByteArray, QByteArray>> out;
    for (const auto &item : list) {
        out.append(item);
    }
    return out;
}

constexpr const char *kMetadataHost = "169.254.169.254";
constexpr const char *kOciMetadataHost = "169.254.0.23";

CloudIdentity probeGcp(int timeoutMs, int &attempted, QString &error) {
    // "169.254.169.254/computeMetadata/v1" + "instance/machine-type" and the
    // Metadata-Flavor request header are the agent's own GCP surface. The
    // metadata service answers without the header with 403, so a non-GCP
    // host costs exactly one request.
    const auto flavor = headers({{QByteArrayLiteral("Metadata-Flavor"),
                                  QByteArrayLiteral("Google")}});
    const HttpResponse machine = httpRequest(
        QLatin1String(kMetadataHost), 80, QByteArrayLiteral("GET"),
        QStringLiteral("/computeMetadata/v1/instance/machine-type"), flavor,
        timeoutMs);
    ++attempted;
    if (!machine.ok) {
        error = machine.error;
        return CloudIdentity{};
    }
    const QString machineType = QString::fromUtf8(machine.body).trimmed();
    if (!machineType.contains(QLatin1String("/machineTypes/"))) {
        error = QStringLiteral("gcp: metadata did not answer with a machine type");
        return CloudIdentity{};
    }
    GcpMetadataFields fields;
    fields.machineType = machine.body;
    const auto get = [&](const char *path) {
        return httpRequest(QLatin1String(kMetadataHost), 80,
                           QByteArrayLiteral("GET"), QLatin1String(path),
                           flavor, timeoutMs)
            .body;
    };
    fields.zone = get("/computeMetadata/v1/instance/zone");
    fields.id = get("/computeMetadata/v1/instance/id");
    fields.name = get("/computeMetadata/v1/instance/name");
    fields.project = get("/computeMetadata/v1/project/project-id");
    attempted += 4;
    CloudIdentity identity = parseGcpInstance(fields);
    if (identity.provider == CloudProvider::None) {
        error = identity.note;
    }
    return identity;
}

CloudIdentity probeOci(int timeoutMs, int &attempted, QString &error) {
    // "Bearer Oracle" + "http://169.254.0.23" + the opc/v2/instance/* paths.
    const auto auth = headers({{QByteArrayLiteral("Authorization"),
                                QByteArrayLiteral("Bearer Oracle")}});
    HttpResponse whole = httpRequest(
        QLatin1String(kOciMetadataHost), 80, QByteArrayLiteral("GET"),
        QStringLiteral("/opc/v2/instance/"), auth, timeoutMs);
    ++attempted;
    if (whole.ok) {
        CloudIdentity identity = parseOciInstance(whole.body);
        if (identity.provider != CloudProvider::None) {
            return identity;
        }
    }
    const HttpResponse id = httpRequest(
        QLatin1String(kOciMetadataHost), 80, QByteArrayLiteral("GET"),
        QStringLiteral("/opc/v2/instance/id"), auth, timeoutMs);
    ++attempted;
    if (!id.ok || id.body.trimmed().isEmpty()) {
        error = id.error.isEmpty() ? whole.error : id.error;
        return CloudIdentity{};
    }
    const HttpResponse shape = httpRequest(
        QLatin1String(kOciMetadataHost), 80, QByteArrayLiteral("GET"),
        QStringLiteral("/opc/v2/instance/shape"), auth, timeoutMs);
    ++attempted;
    CloudIdentity identity;
    identity.provider = CloudProvider::Oci;
    identity.instanceId = QString::fromUtf8(id.body).trimmed();
    identity.instanceType = QString::fromUtf8(shape.body).trimmed();
    identity.operatingSystem = QStringLiteral("linux");
    return identity;
}

CloudIdentity probeAzure(int timeoutMs, int &attempted, QString &error) {
    // "/metadata/instance/compute?api-version=2021-12-13&format=json" with
    // the Metadata: true request header.
    const auto meta = headers(
        {{QByteArrayLiteral("Metadata"), QByteArrayLiteral("true")}});
    const HttpResponse response = httpRequest(
        QLatin1String(kMetadataHost), 80, QByteArrayLiteral("GET"),
        QStringLiteral("/metadata/instance/compute?api-version=2021-12-13"
                       "&format=json"),
        meta, timeoutMs);
    ++attempted;
    if (!response.ok) {
        error = response.error;
        return CloudIdentity{};
    }
    CloudIdentity identity = parseAzureCompute(response.body);
    if (identity.provider == CloudProvider::None) {
        error = identity.note;
    }
    return identity;
}

CloudIdentity probeAws(int timeoutMs, int &attempted, QString &error) {
    // PUT /latest/api/token with the TTL header, then the identity document
    // with X-aws-ec2-metadata-token. A refused token falls back to the
    // IMDSv1 document read (the agent's own bypass path).
    const HttpResponse token = httpRequest(
        QLatin1String(kMetadataHost), 80, QByteArrayLiteral("PUT"),
        QStringLiteral("/latest/api/token"),
        headers({{QByteArrayLiteral("X-aws-ec2-metadata-token-ttl-seconds"),
                  QByteArrayLiteral("21600")}}),
        timeoutMs);
    ++attempted;
    QList<QPair<QByteArray, QByteArray>> extra;
    if (token.ok && !token.body.trimmed().isEmpty()) {
        extra.append(qMakePair(QByteArrayLiteral("X-aws-ec2-metadata-token"),
                               token.body.trimmed()));
    }
    const HttpResponse document = httpRequest(
        QLatin1String(kMetadataHost), 80, QByteArrayLiteral("GET"),
        QStringLiteral("/latest/dynamic/instance-identity/document"), extra,
        timeoutMs);
    ++attempted;
    if (!document.ok) {
        error = document.error;
        return CloudIdentity{};
    }
    CloudIdentity identity = parseAwsIdentityDocument(document.body);
    if (identity.provider == CloudProvider::None) {
        error = identity.note;
    }
    return identity;
}

}  // namespace

CloudProbeResult probeCloudIdentity(const CloudProbeOptions &options) {
    CloudProbeResult result;
    if (!options.enabled) {
        result.error = QStringLiteral(
            "cloud: disabled ([cloud] enabled=false) - no instance metadata "
            "probe runs");
        return result;
    }
    const int timeoutMs = qBound(120, options.timeoutMs, 5000);
    // Unique request headers make each provider distinguishable without a
    // wrong-provider false positive, so the order only decides who answers
    // first on a host that runs more than one (nested virtualisation).
    const CloudIdentity gcp = probeGcp(timeoutMs, result.attempted, result.error);
    if (gcp.provider != CloudProvider::None) {
        result.identity = gcp;
        result.error.clear();
        return result;
    }
    const CloudIdentity oci = probeOci(timeoutMs, result.attempted, result.error);
    if (oci.provider != CloudProvider::None) {
        result.identity = oci;
        result.error.clear();
        return result;
    }
    const CloudIdentity azure =
        probeAzure(timeoutMs, result.attempted, result.error);
    if (azure.provider != CloudProvider::None) {
        result.identity = azure;
        result.error.clear();
        return result;
    }
    const CloudIdentity aws = probeAws(timeoutMs, result.attempted, result.error);
    if (aws.provider != CloudProvider::None) {
        result.identity = aws;
        result.error.clear();
        return result;
    }
    result.error = cloudUnreachableText();
    return result;
}

qint64 cloudHostUptimeSeconds() {
#if defined(Q_OS_LINUX)
    QFile file(QStringLiteral("/proc/uptime"));
    if (!file.open(QIODevice::ReadOnly)) {
        return -1;
    }
    const QByteArray line = file.read(64).trimmed();
    file.close();
    bool ok = false;
    const double seconds = line.split(' ').value(0).toDouble(&ok);
    if (!ok || seconds < 0.0) {
        return -1;
    }
    return qint64(seconds);
#elif defined(Q_OS_MACOS)
    struct timeval boot{};
    size_t size = sizeof(boot);
    if (::sysctlbyname("kern.boottime", &boot, &size, nullptr, 0) != 0) {
        return -1;
    }
    const qint64 now = qint64(::time(nullptr));
    const qint64 started = qint64(boot.tv_sec);
    return now > started ? now - started : -1;
#elif defined(Q_OS_WIN)
    return qint64(::GetTickCount64() / 1000ULL);
#else
    return -1;
#endif
}
