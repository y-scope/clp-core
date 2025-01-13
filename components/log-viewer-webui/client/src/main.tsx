import {StrictMode} from "react";
import {createRoot} from "react-dom/client";

import App from "./App";

import "./index.css";


const rootElement = document.getElementById("root");
if (null === rootElement) {
    throw new Error("Root element not found. " +
        "Please ensure an element with id 'root' exists in the DOM.");
}

const root = createRoot(rootElement);
root.render(
    <StrictMode>
        <App/>
    </StrictMode>
);
