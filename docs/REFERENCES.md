# Referencias · Teltonika LP104

Documentación externa que se usó para entender el hardware, los protocolos y las
libertades del proyecto. No es una bibliografía decorativa: cada enlace apunta a
la fuente que explica una decisión concreta.

## Fuentes técnicas

| Fuente | Para qué se consultó |
|---|---|
| [Teltonika RUT200](https://teltonika-networks.com/products/routers/rut200) | Hardware y firmware del router. |
| [MQTT](https://mqtt.org/) | Especificación y ecosistema del protocolo. |

## Versiones de este repositorio

`lp104MqttTeltonika`, `lp104MqttTeltonikaV2`

Cada carpeta puede tener dependencias y versiones distintas. Cuando el código
depende de una versión específica, esa versión está declarada en el README o en
el archivo de configuración de la carpeta, no en este documento.

## Criterio de preservación

- El código y los documentos se copiaron desde la carpeta original de trabajo.
- Se excluyeron `.git`, dependencias instaladas, compilados, registros y bases
  de datos locales.
- Se redactaron tokens, claves privadas y contraseñas detectadas en texto.
- Las plantillas `.example`, `.sample` y los valores de demostración se
  conservan porque documentan la configuración esperada.

## Limitaciones conocidas

Un router industrial no siempre tiene un cliente MQTT flexible, y compilar credenciales dentro del binario obliga a recompilar para cambiar de red.

La documentación de cada versión describe el alcance real. Si una función no
está implementada, se dice; si un hardware no fue probado, también.
