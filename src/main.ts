import { createApp } from "vue";
import App from "./App.vue";
import Config from "./Config.vue";
import "./style.css";
createApp(location.pathname === "/config" ? Config : App).mount("#app");
