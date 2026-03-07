// system/Server.java - Системный сервер Soiav OS 2 (Java)
package soiav.system;

import java.io.*;
import java.net.*;
import java.nio.*;
import java.nio.channels.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

public class Server {
    
    // ==================== КОНСТАНТЫ ====================
    
    private static final String VERSION = "2.0";
    private static final int PORT = 4242;
    private static final int MAX_CLIENTS = 256;
    private static final int BUFFER_SIZE = 8192;
    private static final int HEARTBEAT_INTERVAL = 5000;
    
    // Типы сообщений
    private static final int MSG_PING = 0;
    private static final int MSG_AUTH = 1;
    private static final int MSG_COMMAND = 2;
    private static final int MSG_FILE = 3;
    private static final int MSG_PROCESS = 4;
    private static final int MSG_SERVICE = 5;
    private static final int MSG_LOG = 6;
    private static final int MSG_SHUTDOWN = 7;
    
    // Статусы
    private static final int STATUS_OK = 200;
    private static final int STATUS_ERROR = 500;
    private static final int STATUS_UNAUTHORIZED = 401;
    private static final int STATUS_NOT_FOUND = 404;
    
    // ==================== КЛАССЫ ====================
    
    // Клиент
    private static class Client {
        int id;
        SocketChannel channel;
        String name;
        String address;
        int port;
        long connectedTime;
        long lastHeartbeat;
        boolean authenticated;
        String user;
        int permissions;
        Map<String, Object> session = new HashMap<>();
        
        ByteBuffer readBuffer = ByteBuffer.allocate(BUFFER_SIZE);
        ByteBuffer writeBuffer = ByteBuffer.allocate(BUFFER_SIZE);
        
        Queue<Message> messageQueue = new ConcurrentLinkedQueue<>();
        
        Client(int id, SocketChannel channel) {
            this.id = id;
            this.channel = channel;
            this.connectedTime = System.currentTimeMillis();
            this.lastHeartbeat = connectedTime;
            
            Socket socket = channel.socket();
            this.address = socket.getInetAddress().getHostAddress();
            this.port = socket.getPort();
        }
    }
    
    // Сообщение
    private static class Message {
        int type;
        int id;
        long timestamp;
        Map<String, Object> headers = new HashMap<>();
        byte[] data;
        
        Message(int type) {
            this.type = type;
            this.id = (int)(Math.random() * Integer.MAX_VALUE);
            this.timestamp = System.currentTimeMillis();
        }
        
        byte[] encode() throws IOException {
            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            DataOutputStream dos = new DataOutputStream(baos);
            
            dos.writeInt(type);
            dos.writeInt(id);
            dos.writeLong(timestamp);
            
            dos.writeInt(headers.size());
            for (Map.Entry<String, Object> entry : headers.entrySet()) {
                dos.writeUTF(entry.getKey());
                if (entry.getValue() instanceof String) {
                    dos.writeByte(0);
                    dos.writeUTF((String)entry.getValue());
                } else if (entry.getValue() instanceof Integer) {
                    dos.writeByte(1);
                    dos.writeInt((Integer)entry.getValue());
                } else if (entry.getValue() instanceof Long) {
                    dos.writeByte(2);
                    dos.writeLong((Long)entry.getValue());
                } else if (entry.getValue() instanceof Boolean) {
                    dos.writeByte(3);
                    dos.writeBoolean((Boolean)entry.getValue());
                }
            }
            
            if (data != null) {
                dos.writeInt(data.length);
                dos.write(data);
            } else {
                dos.writeInt(0);
            }
            
            return baos.toByteArray();
        }
        
        static Message decode(byte[] bytes) throws IOException {
            ByteArrayInputStream bais = new ByteArrayInputStream(bytes);
            DataInputStream dis = new DataInputStream(bais);
            
            Message msg = new Message(dis.readInt());
            msg.id = dis.readInt();
            msg.timestamp = dis.readLong();
            
            int headerCount = dis.readInt();
            for (int i = 0; i < headerCount; i++) {
                String key = dis.readUTF();
                byte type = dis.readByte();
                switch (type) {
                    case 0: msg.headers.put(key, dis.readUTF()); break;
                    case 1: msg.headers.put(key, dis.readInt()); break;
                    case 2: msg.headers.put(key, dis.readLong()); break;
                    case 3: msg.headers.put(key, dis.readBoolean()); break;
                }
            }
            
            int dataLen = dis.readInt();
            if (dataLen > 0) {
                msg.data = new byte[dataLen];
                dis.readFully(msg.data);
            }
            
            return msg;
        }
    }
    
    // Процесс
    private static class Process {
        int pid;
        String name;
        String path;
        String[] args;
        long startTime;
        int cpu;
        int memory;
        int status; // 0=running, 1=stopped, 2=zombie
        ProcessHandle handle;
        
        Process(int pid, String name, String path, String[] args) {
            this.pid = pid;
            this.name = name;
            this.path = path;
            this.args = args;
            this.startTime = System.currentTimeMillis();
            this.status = 0;
        }
    }
    
    // Сервис
    private static class Service {
        String name;
        String className;
        String status; // running, stopped, error
        long startTime;
        Thread thread;
        Object instance;
    }
    
    // Лог
    private static class LogEntry {
        long timestamp;
        int level; // 0=info, 1=warning, 2=error
        String source;
        String message;
        
        LogEntry(int level, String source, String message) {
            this.timestamp = System.currentTimeMillis();
            this.level = level;
            this.source = source;
            this.message = message;
        }
    }
    
    // Статистика
    private static class Stats {
        long startTime = System.currentTimeMillis();
        AtomicLong totalMessages = new AtomicLong();
        AtomicLong totalBytes = new AtomicLong();
        AtomicInteger activeConnections = new AtomicInteger();
        AtomicInteger totalConnections = new AtomicInteger();
    }
    
    // ==================== ПОЛЯ СЕРВЕРА ====================
    
    private static ServerSocketChannel serverChannel;
    private static Selector selector;
    private static Map<Integer, Client> clients = new ConcurrentHashMap<>();
    private static Map<Integer, Process> processes = new HashMap<>();
    private static Map<String, Service> services = new HashMap<>();
    private static List<LogEntry> logs = Collections.synchronizedList(new ArrayList<>());
    private static Stats stats = new Stats();
    
    private static int nextClientId = 1;
    private static int nextPid = 1000;
    private static boolean running = true;
    
    private static ScheduledExecutorService scheduler = Executors.newScheduledThreadPool(4);
    private static ExecutorService workerPool = Executors.newFixedThreadPool(10);
    
    // ==================== ОСНОВНЫЕ МЕТОДЫ ====================
    
    public static void main(String[] args) {
        System.out.println("Soiav System Server v" + VERSION);
        System.out.println("Запуск системных служб...");
        
        try {
            // Инициализация
            initServer();
            
            // Запуск сервисов
            startServices();
            
            // Запуск сервера
            startServer();
            
        } catch (Exception e) {
            System.err.println("Критическая ошибка: " + e.getMessage());
            e.printStackTrace();
        }
    }
    
    private static void initServer() throws IOException {
        // Создание селектора
        selector = Selector.open();
        
        // Создание серверного сокета
        serverChannel = ServerSocketChannel.open();
        serverChannel.configureBlocking(false);
        serverChannel.socket().bind(new InetSocketAddress(PORT));
        serverChannel.register(selector, SelectionKey.OP_ACCEPT);
        
        System.out.println("Сервер слушает порт " + PORT);
        
        // Запуск задач по расписанию
        scheduler.scheduleAtFixedRate(Server::heartbeatCheck, 
                                      HEARTBEAT_INTERVAL, 
                                      HEARTBEAT_INTERVAL, 
                                      TimeUnit.MILLISECONDS);
        
        scheduler.scheduleAtFixedRate(Server::updateStats, 
                                      1, 1, TimeUnit.SECONDS);
    }
    
    private static void startServices() {
        // Регистрация системных сервисов
        registerService("logger", "soiav.system.LoggerService");
        registerService("process-manager", "soiav.system.ProcessManager");
        registerService("file-server", "soiav.system.FileServer");
        registerService("auth", "soiav.system.AuthService");
        registerService("update", "soiav.system.UpdateService");
        
        // Запуск сервисов
        for (Service service : services.values()) {
            startService(service.name);
        }
    }
    
    private static void startServer() throws IOException {
        while (running) {
            selector.select();
            Iterator<SelectionKey> keys = selector.selectedKeys().iterator();
            
            while (keys.hasNext()) {
                SelectionKey key = keys.next();
                keys.remove();
                
                try {
                    if (key.isAcceptable()) {
                        handleAccept(key);
                    } else if (key.isReadable()) {
                        handleRead(key);
                    } else if (key.isWritable()) {
                        handleWrite(key);
                    }
                } catch (Exception e) {
                    log(2, "Server", "Ошибка обработки: " + e.getMessage());
                    key.cancel();
                }
            }
        }
    }
    
    // ==================== ОБРАБОТКА СОЕДИНЕНИЙ ====================
    
    private static void handleAccept(SelectionKey key) throws IOException {
        ServerSocketChannel server = (ServerSocketChannel)key.channel();
        SocketChannel channel = server.accept();
        
        if (channel != null) {
            channel.configureBlocking(false);
            
            int clientId = nextClientId++;
            Client client = new Client(clientId, channel);
            clients.put(clientId, client);
            
            channel.register(selector, SelectionKey.OP_READ, clientId);
            
            stats.activeConnections.incrementAndGet();
            stats.totalConnections.incrementAndGet();
            
            log(0, "Server", "Новое подключение: " + client.address + ":" + 
                client.port + " (ID: " + clientId + ")");
        }
    }
    
    private static void handleRead(SelectionKey key) throws IOException {
        Integer clientId = (Integer)key.attachment();
        Client client = clients.get(clientId);
        
        if (client == null) {
            key.cancel();
            return;
        }
        
        SocketChannel channel = client.channel;
        ByteBuffer buffer = client.readBuffer;
        
        int bytesRead = channel.read(buffer);
        
        if (bytesRead == -1) {
            // Клиент отключился
            disconnectClient(client);
            return;
        }
        
        if (bytesRead > 0) {
            stats.totalBytes.addAndGet(bytesRead);
            buffer.flip();
            
            // Обработка сообщения в пуле потоков
            byte[] messageBytes = new byte[buffer.remaining()];
            buffer.get(messageBytes);
            buffer.clear();
            
            workerPool.execute(() -> processMessage(client, messageBytes));
        }
    }
    
    private static void handleWrite(SelectionKey key) throws IOException {
        Integer clientId = (Integer)key.attachment();
        Client client = clients.get(clientId);
        
        if (client == null) {
            key.cancel();
            return;
        }
        
        Message msg = client.messageQueue.poll();
        if (msg != null) {
            byte[] data = msg.encode();
            client.writeBuffer.clear();
            client.writeBuffer.put(data);
            client.writeBuffer.flip();
            
            while (client.writeBuffer.hasRemaining()) {
                client.channel.write(client.writeBuffer);
            }
            
            stats.totalBytes.addAndGet(data.length);
        }
        
        if (client.messageQueue.isEmpty()) {
            key.interestOps(SelectionKey.OP_READ);
        }
    }
    
    private static void disconnectClient(Client client) {
        try {
            clients.remove(client.id);
            client.channel.close();
            stats.activeConnections.decrementAndGet();
            
            log(0, "Server", "Клиент отключился: " + client.address + 
                " (ID: " + client.id + ")");
        } catch (IOException e) {
            log(2, "Server", "Ошибка при отключении клиента: " + e.getMessage());
        }
    }
    
    // ==================== ОБРАБОТКА СООБЩЕНИЙ ====================
    
    private static void processMessage(Client client, byte[] data) {
        try {
            Message msg = Message.decode(data);
            stats.totalMessages.incrementAndGet();
            
            client.lastHeartbeat = System.currentTimeMillis();
            
            switch (msg.type) {
                case MSG_PING:
                    handlePing(client, msg);
                    break;
                    
                case MSG_AUTH:
                    handleAuth(client, msg);
                    break;
                    
                case MSG_COMMAND:
                    handleCommand(client, msg);
                    break;
                    
                case MSG_FILE:
                    handleFile(client, msg);
                    break;
                    
                case MSG_PROCESS:
                    handleProcess(client, msg);
                    break;
                    
                case MSG_SERVICE:
                    handleService(client, msg);
                    break;
                    
                case MSG_SHUTDOWN:
                    handleShutdown(client, msg);
                    break;
                    
                default:
                    sendError(client, msg, "Неизвестный тип сообщения");
            }
        } catch (Exception e) {
            log(2, "Server", "Ошибка обработки сообщения: " + e.getMessage());
        }
    }
    
    private static void handlePing(Client client, Message msg) throws IOException {
        Message response = new Message(MSG_PING);
        response.headers.put("status", STATUS_OK);
        response.headers.put("version", VERSION);
        response.headers.put("time", System.currentTimeMillis());
        response.headers.put("clients", clients.size());
        response.headers.put("uptime", (System.currentTimeMillis() - stats.startTime) / 1000);
        
        sendMessage(client, response);
    }
    
    private static void handleAuth(Client client, Message msg) {
        String user = (String)msg.headers.get("user");
        String password = (String)msg.headers.get("password");
        
        // Проверка аутентификации (упрощенно)
        if (user != null && password != null) {
            if (authenticate(user, password)) {
                client.authenticated = true;
                client.user = user;
                client.permissions = 7; // Полные права
                
                Message response = new Message(MSG_AUTH);
                response.headers.put("status", STATUS_OK);
                response.headers.put("session", UUID.randomUUID().toString());
                sendMessage(client, response);
                
                log(0, "Auth", "Пользователь " + user + " авторизован");
            } else {
                sendError(client, msg, "Неверные учетные данные");
            }
        } else {
            sendError(client, msg, "Требуется аутентификация");
        }
    }
    
    private static void handleCommand(Client client, Message msg) {
        if (!checkAuth(client)) return;
        
        String command = (String)msg.headers.get("command");
        
        if (command == null) {
            sendError(client, msg, "Команда не указана");
            return;
        }
        
        try {
            String result = executeCommand(command);
            
            Message response = new Message(MSG_COMMAND);
            response.headers.put("status", STATUS_OK);
            response.headers.put("result", result);
            sendMessage(client, response);
        } catch (Exception e) {
            sendError(client, msg, e.getMessage());
        }
    }
    
    private static void handleFile(Client client, Message msg) {
        if (!checkAuth(client)) return;
        
        String action = (String)msg.headers.get("action");
        String path = (String)msg.headers.get("path");
        
        if (action == null || path == null) {
            sendError(client, msg, "Недостаточно параметров");
            return;
        }
        
        try {
            switch (action) {
                case "list":
                    handleFileList(client, msg, path);
                    break;
                    
                case "read":
                    handleFileRead(client, msg, path);
                    break;
                    
                case "write":
                    handleFileWrite(client, msg, path);
                    break;
                    
                case "delete":
                    handleFileDelete(client, msg, path);
                    break;
                    
                default:
                    sendError(client, msg, "Неизвестное действие: " + action);
            }
        } catch (Exception e) {
            sendError(client, msg, e.getMessage());
        }
    }
    
    private static void handleFileList(Client client, Message msg, String path) {
        File dir = new File(path);
        if (!dir.exists() || !dir.isDirectory()) {
            sendError(client, msg, "Директория не существует");
            return;
        }
        
        StringBuilder sb = new StringBuilder();
        for (File file : dir.listFiles()) {
            sb.append(file.getName());
            sb.append(file.isDirectory() ? "/" : "");
            sb.append("\t");
            sb.append(file.length());
            sb.append("\t");
            sb.append(new Date(file.lastModified()));
            sb.append("\n");
        }
        
        Message response = new Message(MSG_FILE);
        response.headers.put("status", STATUS_OK);
        response.headers.put("action", "list");
        response.headers.put("path", path);
        response.data = sb.toString().getBytes();
        sendMessage(client, response);
    }
    
    private static void handleFileRead(Client client, Message msg, String path) throws IOException {
        File file = new File(path);
        if (!file.exists() || !file.isFile()) {
            sendError(client, msg, "Файл не существует");
            return;
        }
        
        try (FileInputStream fis = new FileInputStream(file)) {
            byte[] data = new byte[(int)file.length()];
            fis.read(data);
            
            Message response = new Message(MSG_FILE);
            response.headers.put("status", STATUS_OK);
            response.headers.put("action", "read");
            response.headers.put("path", path);
            response.headers.put("size", file.length());
            response.data = data;
            sendMessage(client, response);
        }
    }
    
    private static void handleFileWrite(Client client, Message msg, String path) throws IOException {
        if (msg.data == null) {
            sendError(client, msg, "Нет данных для записи");
            return;
        }
        
        try (FileOutputStream fos = new FileOutputStream(path)) {
            fos.write(msg.data);
        }
        
        Message response = new Message(MSG_FILE);
        response.headers.put("status", STATUS_OK);
        response.headers.put("action", "write");
        response.headers.put("path", path);
        response.headers.put("size", msg.data.length);
        sendMessage(client, response);
    }
    
    private static void handleFileDelete(Client client, Message msg, String path) {
        File file = new File(path);
        if (file.delete()) {
            Message response = new Message(MSG_FILE);
            response.headers.put("status", STATUS_OK);
            response.headers.put("action", "delete");
            response.headers.put("path", path);
            sendMessage(client, response);
        } else {
            sendError(client, msg, "Не удалось удалить файл");
        }
    }
    
    private static void handleProcess(Client client, Message msg) {
        if (!checkAuth(client)) return;
        
        String action = (String)msg.headers.get("action");
        
        try {
            switch (action) {
                case "list":
                    handleProcessList(client, msg);
                    break;
                    
                case "start":
                    handleProcessStart(client, msg);
                    break;
                    
                case "stop":
                    handleProcessStop(client, msg);
                    break;
                    
                case "info":
                    handleProcessInfo(client, msg);
                    break;
                    
                default:
                    sendError(client, msg, "Неизвестное действие: " + action);
            }
        } catch (Exception e) {
            sendError(client, msg, e.getMessage());
        }
    }
    
    private static void handleProcessList(Client client, Message msg) {
        StringBuilder sb = new StringBuilder();
        sb.append("PID\tNAME\tCPU\tMEM\tSTATUS\tUPTIME\n");
        
        synchronized (processes) {
            for (Process proc : processes.values()) {
                long uptime = (System.currentTimeMillis() - proc.startTime) / 1000;
                sb.append(proc.pid).append("\t");
                sb.append(proc.name).append("\t");
                sb.append(proc.cpu).append("%\t");
                sb.append(proc.memory).append("MB\t");
                sb.append(proc.status == 0 ? "RUN" : proc.status == 1 ? "STP" : "ZOM");
                sb.append("\t").append(uptime).append("s\n");
            }
        }
        
        Message response = new Message(MSG_PROCESS);
        response.headers.put("status", STATUS_OK);
        response.headers.put("action", "list");
        response.data = sb.toString().getBytes();
        sendMessage(client, response);
    }
    
    private static void handleProcessStart(Client client, Message msg) {
        String name = (String)msg.headers.get("name");
        String path = (String)msg.headers.get("path");
        
        if (name == null || path == null) {
            sendError(client, msg, "Имя и путь обязательны");
            return;
        }
        
        try {
            ProcessBuilder pb = new ProcessBuilder(path);
            Process proc = pb.start();
            
            int pid = nextPid++;
            Process soiavProc = new Process(pid, name, path, new String[]{path});
            soiavProc.handle = proc.toHandle();
            
            synchronized (processes) {
                processes.put(pid, soiavProc);
            }
            
            // Мониторинг процесса
            CompletableFuture.runAsync(() -> {
                try {
                    int exitCode = proc.waitFor();
                    soiavProc.status = exitCode == 0 ? 0 : 2;
                    log(0, "Process", "Процесс " + pid + " завершился с кодом " + exitCode);
                } catch (InterruptedException e) {
                    // Ignore
                }
            });
            
            Message response = new Message(MSG_PROCESS);
            response.headers.put("status", STATUS_OK);
            response.headers.put("action", "start");
            response.headers.put("pid", pid);
            sendMessage(client, response);
            
            log(0, "Process", "Запущен процесс " + name + " (PID: " + pid + ")");
            
        } catch (IOException e) {
            sendError(client, msg, "Ошибка запуска процесса: " + e.getMessage());
        }
    }
    
    private static void handleProcessStop(Client client, Message msg) {
        Integer pid = (Integer)msg.headers.get("pid");
        
        if (pid == null) {
            sendError(client, msg, "PID не указан");
            return;
        }
        
        synchronized (processes) {
            Process proc = processes.get(pid);
            if (proc != null && proc.handle != null) {
                proc.handle.destroy();
                proc.status = 1;
                
                Message response = new Message(MSG_PROCESS);
                response.headers.put("status", STATUS_OK);
                response.headers.put("action", "stop");
                response.headers.put("pid", pid);
                sendMessage(client, response);
                
                log(0, "Process", "Остановлен процесс " + pid);
            } else {
                sendError(client, msg, "Процесс не найден");
            }
        }
    }
    
    private static void handleProcessInfo(Client client, Message msg) {
        Integer pid = (Integer)msg.headers.get("pid");
        
        if (pid == null) {
            sendError(client, msg, "PID не указан");
            return;
        }
        
        synchronized (processes) {
            Process proc = processes.get(pid);
            if (proc != null) {
                Message response = new Message(MSG_PROCESS);
                response.headers.put("status", STATUS_OK);
                response.headers.put("action", "info");
                response.headers.put("pid", proc.pid);
                response.headers.put("name", proc.name);
                response.headers.put("path", proc.path);
                response.headers.put("startTime", proc.startTime);
                response.headers.put("cpu", proc.cpu);
                response.headers.put("memory", proc.memory);
                response.headers.put("status", proc.status);
                sendMessage(client, response);
            } else {
                sendError(client, msg, "Процесс не найден");
            }
        }
    }
    
    private static void handleService(Client client, Message msg) {
        if (!checkAuth(client)) return;
        
        String action = (String)msg.headers.get("action");
        String serviceName = (String)msg.headers.get("service");
        
        try {
            switch (action) {
                case "list":
                    handleServiceList(client, msg);
                    break;
                    
                case "start":
                    if (serviceName != null) startService(serviceName);
                    sendOk(client, msg);
                    break;
                    
                case "stop":
                    if (serviceName != null) stopService(serviceName);
                    sendOk(client, msg);
                    break;
                    
                case "restart":
                    if (serviceName != null) {
                        stopService(serviceName);
                        Thread.sleep(1000);
                        startService(serviceName);
                    }
                    sendOk(client, msg);
                    break;
                    
                case "status":
                    handleServiceStatus(client, msg, serviceName);
                    break;
                    
                default:
                    sendError(client, msg, "Неизвестное действие: " + action);
            }
        } catch (Exception e) {
            sendError(client, msg, e.getMessage());
        }
    }
    
    private static void handleServiceList(Client client, Message msg) {
        StringBuilder sb = new StringBuilder();
        sb.append("SERVICE\tSTATUS\tUPTIME\n");
        
        for (Service service : services.values()) {
            long uptime = service.startTime > 0 ? 
                (System.currentTimeMillis() - service.startTime) / 1000 : 0;
            sb.append(service.name).append("\t");
            sb.append(service.status).append("\t");
            sb.append(uptime).append("s\n");
        }
        
        Message response = new Message(MSG_SERVICE);
        response.headers.put("status", STATUS_OK);
        response.headers.put("action", "list");
        response.data = sb.toString().getBytes();
        sendMessage(client, response);
    }
    
    private static void handleServiceStatus(Client client, Message msg, String serviceName) {
        if (serviceName == null) {
            sendError(client, msg, "Имя сервиса не указано");
            return;
        }
        
        Service service = services.get(serviceName);
        if (service == null) {
            sendError(client, msg, "Сервис не найден");
            return;
        }
        
        Message response = new Message(MSG_SERVICE);
        response.headers.put("status", STATUS_OK);
        response.headers.put("action", "status");
        response.headers.put("service", service.name);
        response.headers.put("statusText", service.status);
        response.headers.put("startTime", service.startTime);
        sendMessage(client, response);
    }
    
    private static void handleShutdown(Client client, Message msg) {
        if (!checkAuth(client) || client.permissions < 7) {
            sendError(client, msg, "Недостаточно прав");
            return;
        }
        
        log(0, "Server", "Получена команда на выключение от " + client.user);
        
        // Остановка сервисов
        for (Service service : services.values()) {
            if ("running".equals(service.status)) {
                stopService(service.name);
            }
        }
        
        // Отправка подтверждения
        Message response = new Message(MSG_SHUTDOWN);
        response.headers.put("status", STATUS_OK);
        sendMessage(client, response);
        
        // Завершение работы
        running = false;
        scheduler.shutdown();
        workerPool.shutdown();
        
        try {
            Thread.sleep(1000);
        } catch (InterruptedException e) {}
        
        System.exit(0);
    }
    
    // ==================== СЛУЖЕБНЫЕ МЕТОДЫ ====================
    
    private static boolean authenticate(String user, String password) {
        // Упрощенная аутентификация
        return "admin".equals(user) && "admin".equals(password);
    }
    
    private static boolean checkAuth(Client client) {
        if (!client.authenticated) {
            sendError(client, null, "Требуется аутентификация");
            return false;
        }
        return true;
    }
    
    private static String executeCommand(String command) {
        // Упрощенное выполнение команд
        if (command.equals("date")) {
            return new Date().toString();
        } else if (command.equals("uptime")) {
            long uptime = (System.currentTimeMillis() - stats.startTime) / 1000;
            return "Uptime: " + uptime + " seconds";
        } else if (command.startsWith("echo ")) {
            return command.substring(5);
        } else {
            return "Unknown command: " + command;
        }
    }
    
    private static void registerService(String name, String className) {
        Service service = new Service();
        service.name = name;
        service.className = className;
        service.status = "stopped";
        services.put(name, service);
        
        log(0, "Service", "Зарегистрирован сервис: " + name);
    }
    
    private static void startService(String name) {
        Service service = services.get(name);
        if (service == null) return;
        
        if ("running".equals(service.status)) return;
        
        try {
            Class<?> clazz = Class.forName(service.className);
            service.instance = clazz.newInstance();
            service.thread = new Thread(() -> {
                try {
                    clazz.getMethod("start").invoke(service.instance);
                } catch (Exception e) {
                    log(2, "Service", "Ошибка в сервисе " + name + ": " + e.getMessage());
                }
            });
            service.thread.setName("Service-" + name);
            service.thread.start();
            
            service.status = "running";
            service.startTime = System.currentTimeMillis();
            
            log(0, "Service", "Запущен сервис: " + name);
        } catch (Exception e) {
            service.status = "error";
            log(2, "Service", "Ошибка запуска " + name + ": " + e.getMessage());
        }
    }
    
    private static void stopService(String name) {
        Service service = services.get(name);
        if (service == null || !"running".equals(service.status)) return;
        
        try {
            if (service.instance != null) {
                service.instance.getClass().getMethod("stop").invoke(service.instance);
            }
            service.thread.interrupt();
            service.thread.join(5000);
            service.status = "stopped";
            
            log(0, "Service", "Остановлен сервис: " + name);
        } catch (Exception e) {
            log(2, "Service", "Ошибка остановки " + name + ": " + e.getMessage());
        }
    }
    
    private static void sendMessage(Client client, Message msg) {
        client.messageQueue.add(msg);
        
        SelectionKey key = client.channel.keyFor(selector);
        if (key != null) {
            key.interestOps(SelectionKey.OP_READ | SelectionKey.OP_WRITE);
            selector.wakeup();
        }
    }
    
    private static void sendOk(Client client, Message request) {
        Message response = new Message(request.type);
        response.headers.put("status", STATUS_OK);
        sendMessage(client, response);
    }
    
    private static void sendError(Client client, Message request, String error) {
        Message response = new Message(request != null ? request.type : MSG_COMMAND);
        response.headers.put("status", STATUS_ERROR);
        response.headers.put("error", error);
        if (client != null) {
            sendMessage(client, response);
        }
        log(2, "Server", "Ошибка: " + error);
    }
    
    private static void log(int level, String source, String message) {
        LogEntry entry = new LogEntry(level, source, message);
        logs.add(entry);
        if (logs.size() > 10000) {
            logs.remove(0);
        }
        
        String prefix;
        switch (level) {
            case 0: prefix = "INFO"; break;
            case 1: prefix = "WARN"; break;
            case 2: prefix = "ERROR"; break;
            default: prefix = "LOG"; break;
        }
        
        System.out.println("[" + prefix + "] " + source + ": " + message);
    }
    
    private static void heartbeatCheck() {
        long now = System.currentTimeMillis();
        long timeout = HEARTBEAT_INTERVAL * 3;
        
        for (Client client : clients.values()) {
            if (now - client.lastHeartbeat > timeout) {
                log(1, "Server", "Клиент " + client.id + " не отвечает, отключаем");
                disconnectClient(client);
            }
        }
    }
    
    private static void updateStats() {
        // Обновление статистики процессов
        synchronized (processes) {
            for (Process proc : processes.values()) {
                if (proc.handle != null && proc.handle.isAlive()) {
                    proc.cpu = (int)(Math.random() * 10); // Заглушка
                    proc.memory = (int)(Math.random() * 200); // Заглушка
                }
            }
        }
    }
    
    // ==================== СЕРВИСЫ ====================
    
    public static class LoggerService {
        public void start() {
            // Запуск логгера
        }
        public void stop() {
            // Остановка логгера
        }
    }
    
    public static class ProcessManager {
        public void start() {
            // Запуск менеджера процессов
        }
        public void stop() {
            // Остановка менеджера процессов
        }
    }
    
    public static class FileServer {
        public void start() {
            // Запуск файлового сервера
        }
        public void stop() {
            // Остановка файлового сервера
        }
    }
    
    public static class AuthService {
        public void start() {
            // Запуск сервиса аутентификации
        }
        public void stop() {
            // Остановка сервиса аутентификации
        }
    }
    
    public static class UpdateService {
        public void start() {
            // Запуск сервиса обновлений
        }
        public void stop() {
            // Остановка сервиса обновлений
        }
    }
}
