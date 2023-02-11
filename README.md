# WebBench
C语言实现的Web压测工具.它通过使用fork模拟多个客户端同时向指定URL发送Http Request.


## 命令行选项

```
-f | --force          不需要等待服务器响应
-r | --reload         不允许返回缓存的数据，需要从源服务器中重新读取最新数据
-t | --time           模拟的每一个客户端发送Http Request的时间
-p | --proxy<ip:port> 使用代理服务器来发送请求
-c | --clients        模拟的客户端数目
-9 | --http09         使用HTTP/0.9
-1 | --http10         使用HTTP/1.0
-2 | --http11         使用HTTP/1.1
     --get            使用GET方法
     --head           使用HEAD方法
     --options        使用OPTIONS方法
     --trace          使用TRACE方法
-h | --help           打印帮助信息
-V                    打印版本号
```