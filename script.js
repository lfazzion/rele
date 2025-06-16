document.getElementById("connect").addEventListener("click", async () => {
  try {
    const device = await navigator.bluetooth.requestDevice({
      filters: [{ namePrefix: "ESP32" }],
      optionalServices: ["0000ffe0-0000-1000-8000-00805f9b34fb"], // UUID do serviço
    });

    const server = await device.gatt.connect();
    document.getElementById("status").textContent = "Status: Conectado";

    const service = await server.getPrimaryService(
      "0000ffe0-0000-1000-8000-00805f9b34fb",
    );
    const characteristic = await service.getCharacteristic(
      "0000ffe1-0000-1000-8000-00805f9b34fb",
    );

    await characteristic.startNotifications();
    characteristic.addEventListener("characteristicvaluechanged", (event) => {
      const decoder = new TextDecoder("utf-8");
      const value = decoder.decode(event.target.value);
      document.getElementById("dados").textContent += value + "\n";
    });
  } catch (error) {
    document.getElementById("status").textContent = "Erro: " + error;
  }
});
