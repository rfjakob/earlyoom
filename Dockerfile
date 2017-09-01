FROM gcc as build

WORKDIR /usr/src
COPY . .

ENV CFLAGS -static
RUN make

###

FROM scratch
COPY --from=build /usr/src/earlyoom /

ENTRYPOINT ["/earlyoom"]
