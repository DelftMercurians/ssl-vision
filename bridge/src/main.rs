use std::io::{self, Write};
use std::net::{TcpListener, UdpSocket};
use std::sync::{Arc, Mutex};
use std::thread;

fn main() -> io::Result<()> {
    println!("Starting bridge");

    // Set up TCP listener on port 6078
    let listener = TcpListener::bind("0.0.0.0:6078")?;
    let clients = Arc::new(Mutex::new(Vec::new()));

    // Handling TCP client connections in separate thread
    let clients_clone = clients.clone();
    thread::spawn(move || {
        for stream in listener.incoming() {
            match stream {
                Ok(stream) => {
                    println!("New client connected");
                    clients_clone.lock().unwrap().push(stream);
                }
                Err(e) => {
                    println!("Connection failed: {}", e);
                }
            }
        }
    });

    // Start vision multicast listener
    let vision_socket = UdpSocket::bind("0.0.0.0:10006")?;
    vision_socket.join_multicast_v4(&"224.5.23.2".parse().unwrap(), &"0.0.0.0".parse().unwrap())?;

    let mut buf = [0u8; 2 * 1024];
    loop {
        match vision_socket.recv_from(&mut buf) {
            Ok((size, _)) => {
                let data = &buf[..size];
                let mut clients = clients.lock().unwrap();
                clients.retain(|mut client| match client.write_all(data) {
                    Ok(_) => true,
                    Err(_) => {
                        println!("Client disconnected");
                        false
                    }
                });
            }
            Err(e) => println!("Vision receive error: {}", e),
        }
    }
}
