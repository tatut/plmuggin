FROM postgres:18.3
RUN apt-get update && apt-get install -y build-essential postgresql-server-dev-18
RUN mkdir /opt/plmuggin
COPY . /opt/plmuggin
RUN cd /opt/plmuggin && make install