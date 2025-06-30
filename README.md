# Interface Web para ESP32 e Firebase com Web Bluetooth

Este projeto é uma página web, projetada para ser hospedada no GitHub Pages, que atua como uma ponte entre um microcontrolador ESP32 e o banco de dados Firebase.

## 🚀 Funcionalidades

-   **Conexão Bluetooth:** Conecta-se a um ESP32 usando a API Web Bluetooth.
-   **Visualização de Dados em Tempo Real:** Exibe dados recebidos do ESP32 instantaneamente.
-   **Integração com Firebase:** Armazena cada leitura recebida no Firebase Realtime Database.
-   **Histórico de Dados:** Busca e exibe as últimas 10 leituras diretamente do Firebase.
-   **Envio de Comandos:** Envia um comando de volta para o ESP32 para acionar ações, como uma calibração.
-   **Interface Responsiva:** Simples e funcional em desktops e dispositivos móveis (que suportem Web Bluetooth).

<!--![Demonstração da Interface](https://)
*(Nota: Imagem ilustrativa da estrutura da página)*-->

## 🛠️ Como Usar e Configurar

Siga estes passos para colocar o projeto em funcionamento.

### Pré-requisitos

1.  Um microcontrolador **ESP32**.
2.  **Arduino IDE** ou **PlatformIO** configurado para o ESP32.
3.  Um navegador compatível com **Web Bluetooth** (ex: Google Chrome, Edge em Desktop ou Android).

---

### Passo 1: Configuração do Firebase

1.  Acesse o [console do Firebase](https://console.firebase.google.com/) e crie um novo projeto.
2.  No painel do seu projeto, clique em **"Realtime Database"** e crie um novo banco de dados no modo de **teste** (ou configure as regras de segurança manualmente).
3.  Vá para a aba **"Regras"** e cole o seguinte para permitir leitura e escrita (para fins de desenvolvimento):
    ```json
    {
      "rules": {
        ".read": "true",
        ".write": "true"
      }
    }
    ```
4.  Volte para a página principal do seu projeto, clique no ícone de engrenagem > **Configurações do Projeto**.
5.  Na seção "Seus apps", clique no ícone da web (`</>`) para registrar um novo app da web.
6.  Dê um nome ao seu app e clique em "Registrar app". O Firebase fornecerá um objeto de configuração chamado `firebaseConfig`. **Copie este objeto.**

---

### Passo 2: Configuração do Projeto Web

1.  **Clone ou baixe** este repositório.
2.  Abra o arquivo `script.js` em um editor de texto.
3.  **Cole o seu `firebaseConfig`** que você copiou do Firebase no local indicado no topo do arquivo.

    ```javascript
    // Cole aqui a configuração do seu projeto Firebase
    const firebaseConfig = {
      apiKey: "SUA_API_KEY",
      authDomain: "SEU_AUTH_DOMAIN",
      // ...e o resto das suas credenciais
    };
    ```

4.  Opcionalmente, você pode alterar os UUIDs do serviço e das características Bluetooth se desejar. Lembre-se que eles **precisam ser idênticos** aos definidos no código do seu ESP32.

    ```javascript
    const BLE_SERVICE_UUID = '4fafc201-1fb5-459e-8fcc-c5c9c331914b';
    const BLE_RECEIVE_CHARACTERISTIC_UUID = 'beb5483e-36e1-4688-b7f5-ea07361b26a8';
    const BLE_SEND_CHARACTERISTIC_UUID = 'a4d23253-2778-436c-9c23-2c1b50d87635';
    ```

---

### Passo 3: Configuração do ESP32

Você precisa programar seu ESP32 para criar um servidor BLE com o serviço e as características correspondentes aos UUIDs definidos no `script.js`.

-   **Serviço BLE:** Use o UUID `BLE_SERVICE_UUID`.
-   **Característica de Leitura (Notificação):** Crie uma característica com o UUID `BLE_RECEIVE_CHARACTERISTIC_UUID`. Ela deve ter a propriedade `NOTIFY`. O ESP32 deve escrever os dados de tensão (como um `float`) nesta característica.
-   **Característica de Escrita (Comando):** Crie uma característica com o UUID `BLE_SEND_CHARACTERISTIC_UUID`. Ela deve ter a propriedade `WRITE`. O ESP32 deve monitorar escritas nesta característica para receber comandos (como o comando de calibração).

> **Nota:** Um código de exemplo para o ESP32 não está incluído neste repositório, mas você pode encontrar excelentes exemplos para a biblioteca `BLE-Arduino` na internet.

---

### Passo 4: Hospedagem no GitHub Pages

1.  Crie um novo repositório no seu GitHub.
2.  Envie os arquivos `index.html`, `style.css` e `script.js` para este repositório.
3.  No seu repositório, vá para **Settings > Pages**.
4.  Na seção "Build and deployment", em "Source", selecione **"Deploy from a branch"**.
5.  Escolha a branch `main` (ou `master`) e a pasta `/ (root)`. Clique em **Save**.
6.  Aguarde alguns minutos. O GitHub irá publicar sua página e fornecer a URL de acesso.

Agora você pode acessar a URL fornecida e começar a testar a comunicação!

## 📄 Estrutura do Código

-   **`index.html`**: A estrutura principal da página, contendo os elementos da interface do usuário.
-   **`style.css`**: Contém todos os estilos para tornar a interface visualmente agradável, incluindo as cores de status.
-   **`script.js`**: O cérebro da aplicação. Lida com:
    -   Inicialização do Firebase.
    -   Lógica da API Web Bluetooth para conexão.
    -   Manipulação de eventos para recebimento de dados e desconexão.
    -   Funções para enviar dados ao Firebase e buscar o histórico.
    -   Função para enviar o comando de calibração de volta ao ESP32.

## ⚖️ Licença

Este projeto está sob a licença MIT. Veja o arquivo `LICENSE` para mais detalhes.
